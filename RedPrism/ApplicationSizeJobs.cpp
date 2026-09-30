#include "Application.h"

#include "ChromeHost.h"
#include "SizingWorkTestGate.h"
#include "resource.h"

#include <cstdio>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

class Application::SizingJob final : public Job
{
  public:
    SizingJob(Application& owner, HistoryWorkStamp stamp, JobToken token, CanvasDocument document,
              std::variant<CanvasSizeOptions, ImageSizeOptions> options)
        : _owner(owner), _stamp(stamp), _token(std::move(token)), _source(std::move(document)),
          _options(std::move(options)), _invocation(owner._sizeInvocation), _gate(owner._sizingWorkTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }

    JobResult Run(JobContext& context) override
    {
        if (_gate && !_gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        if (_gate && _gate->throwAllocationFailure)
            throw std::bad_alloc{}; // explicitly injected harness failure, translated by the shared runner
        _status = std::visit(
            [&](const auto& options)
            {
                using Options = std::decay_t<decltype(options)>;
                if constexpr (std::is_same_v<Options, CanvasSizeOptions>)
                    return ResizeCanvas(_source, options, _result, context.cancellation);
                else
                    return ResizeImage(_source, options, _result, context.cancellation);
            },
            _options);
        if (_gate && _gate->status)
            _status = _gate->status.value();
        if (_gate && _gate->afterPreparation)
        {
            const auto result = PassGate(context);
            if (result.status != JobStatus::Finished)
                return result;
        }
        return context.cancellation.IsCancelled() || _status == ResizeStatus::Cancelled
                   ? JobResult{JobStatus::Cancelled}
                   : JobResult{};
    }

    void Completed(JobResult result) noexcept override
    {
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (_owner._sizingStamp != _stamp || !_owner._historyWork.IsCurrent(_stamp) ||
            _owner._sizeInvocation != _invocation || !_owner._session.CanApplyJobResult(_token, _token.sequence))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        bool changed = false;
        if (outcome == HostOutcome::Succeeded)
        {
            switch (_status)
            {
            case ResizeStatus::Ok:
                changed = _owner._session.ApplyDocumentSizeIfCurrent(
                    std::move(_result),
                    std::holds_alternative<CanvasSizeOptions>(_options) ? L"Canvas Size" : L"Image Size", _token);
                if (!changed)
                {
                    outcome = HostOutcome::Failed;
                    error = E_FAIL;
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
                                        error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ? IDS_ERROR_TOO_LARGE
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
            _owner._frameInvalidated = true;
        }
        (void)_owner._historyWork.Finish(_stamp);
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
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
    Application& _owner; // owner callback only; worker preparation reads only the captured values
    const HistoryWorkStamp _stamp;
    const JobToken _token;
    const CanvasDocument _source;
    const std::variant<CanvasSizeOptions, ImageSizeOptions> _options;
    const std::uint64_t _invocation;
    const std::shared_ptr<SizingWorkTestGate> _gate;
    const DWORD _ownerThread;
    CanvasDocument _result;
    ResizeStatus _status = ResizeStatus::Invalid;
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
        const auto token = _session.CaptureJobToken();
        const auto memory =
            std::visit([&](const auto& size) { return SizingMemoryBytes(_session.Document().value(), size); }, options);
        if (token && memory != 0)
        {
            std::unique_ptr<Job> job = std::make_unique<SizingJob>(*this, _sizingStamp.value(), token.value(),
                                                                   _session.Document().value(), std::move(options));
            JobTicket ticket;
            result =
                static_cast<HRESULT>(_jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
            if (SUCCEEDED(result))
                _sizingTicket = ticket;
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // owning admission boundary; the complete request fails without partial acceptance
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
        Reopen
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        Guid revision;
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
        state.held = owner._session.Document() && owner._session.Document()->width == 8 &&
                     owner._session.Document()->height == 4 && owner._historyWork.Busy() && owner._sizingTicket &&
                     owner._jobs.ReservedMemoryBytes() != 0 && state.receipt.Read().outcome == HostOutcome::Pending;
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
    for (const auto mode : {Mode::CopiedCanvas, Mode::CopiedImage, Mode::PanelThenSizeThenUndo, Mode::CancelBefore,
                            Mode::CancelAfter, Mode::Failure, Mode::Allocation, Mode::TooLarge, Mode::Invalid,
                            Mode::Memory, Mode::Unchanged, Mode::Stale, Mode::Restore, Mode::Replace, Mode::Reopen})
    {
        _session.CreateDocument(8, 4);
        (void)InsertPaintedLayer(_session, L"Sizing source", 8, 4, {0, 0}, 0, 0xFF404040);
        _session.history.MarkSaved();
        _session.SetForegroundColor(0xFFFF0000);
        _viewport.followsFit = false;
        DocumentChanged();
        const auto revision = _session.history.CurrentRevision();
        const auto historyCount = _session.history.UndoCount();
        const auto presents = _renderer.PresentCount();
        _sizingWorkTestGate = std::make_shared<SizingWorkTestGate>();
        if (!_sizingWorkTestGate->release)
            return 5;
        _sizingWorkTestGate->afterPreparation = mode != Mode::CancelBefore;
        _sizingWorkTestGate->throwAllocationFailure = mode == Mode::Allocation;
        if (mode == Mode::Allocation)
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
        State state{*this, mode, QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation}), revision};
        std::optional<ContinuationReceipt> undo;
        if (mode == Mode::PanelThenSizeThenUndo)
            undo = QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto actual = state.receipt.Read();
        const auto expected =
            mode == Mode::CancelBefore || mode == Mode::CancelAfter ? HostOutcome::Cancelled
            : mode == Mode::Stale || mode == Mode::Restore || mode == Mode::Replace || mode == Mode::Reopen
                ? HostOutcome::Stale
            : mode == Mode::Failure || mode == Mode::Allocation || mode == Mode::TooLarge || mode == Mode::Invalid ||
                    mode == Mode::Memory
                ? HostOutcome::Failed
                : HostOutcome::Succeeded;
        bool correct = settled == SettleResult::Settled && state.visited && state.held && actual.outcome == expected &&
                       !_historyWork.Busy() && !_sizingTicket && !_sizingStamp && _jobs.ReservedMemoryBytes() == 0 &&
                       _renderer.PresentCount() == presents;
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
            if (mode == Mode::Allocation || mode == Mode::Memory)
                correct = correct && actual.error == E_OUTOFMEMORY;
            if (mode == Mode::TooLarge)
                correct = correct && actual.error == HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            if (mode == Mode::Invalid)
                correct = correct && actual.error == E_INVALIDARG;
        }
        _sizingWorkTestGate.reset();
        CancelSizeCard();
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
    }
    return 0;
}
