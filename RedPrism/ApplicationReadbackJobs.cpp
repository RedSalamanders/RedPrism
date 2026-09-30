#include "Application.h"

#include "ChromeHost.h"
#include "DocumentReadbackJob.h"
#include "UtilityWorkTestGate.h"
#include "resource.h"

#include <cstdio>
#include <memory>
#include <new>
#include <utility>

class Application::UtilityReadbackJob final : public DocumentReadbackJob
{
  public:
    UtilityReadbackJob(Application& owner, HistoryWorkStamp stamp, JobToken token, CanvasDocument document,
                       UtilityReadbackKind kind, std::optional<DocumentSession::MergePlan> merge,
                       std::optional<Guid> adjustment)
        : DocumentReadbackJob(
              owner._jobs, owner._gpuWork, owner._composition, std::move(document),
              {0, 0, 0, static_cast<UINT>(token.canvasWidth), static_cast<UINT>(token.canvasHeight), 1}),
          _owner(owner), _stamp(stamp), _token(std::move(token)), _kind(kind), _merge(std::move(merge)),
          _adjustment(adjustment), _gate(owner._utilityWorkTestGate), _ownerThread(GetCurrentThreadId())
    {
    }

  private:
    HRESULT PrepareReadbackDevice(HRESULT reason) noexcept override
    {
        return _owner.PrepareUtilityDevice(reason);
    }
    JobResult ProcessReadback(Raster& image, JobContext& context) override
    {
        if (_kind == UtilityReadbackKind::Merge)
        {
            _prepared = DocumentSession::PrepareMerge(_merge.value(), image, context.cancellation);
            image = {}; // release the full readback on this worker once the cropped result owns its bytes
            if (context.cancellation.IsCancelled())
                return {JobStatus::Cancelled};
            if (!_prepared)
                return {JobStatus::Failed, E_FAIL};
        }
        if (_gate && _gate->visits.fetch_add(1) == 0)
        {
            if (!PostThreadMessageW(_ownerThread, UtilityWorkTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            if (waited != WAIT_OBJECT_0)
                return {JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
            return _gate->result;
        }
        return {};
    }
    void ResetReadback() noexcept override
    {
        _prepared.reset();
    }
    void ReadbackCompleted(JobResult result, Raster image, std::uint64_t deviceGeneration) noexcept override
    {
        // A result validates the full snapshot: hidden/live-mask ancestors and merge survivors influence pixels.
        // Holding the history ordinal prevents a younger command from snapshotting before this apply/drop.
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (_owner._utilityStamp != _stamp || !_owner._historyWork.IsCurrent(_stamp) ||
            !_owner._session.CanApplyJobResult(_token, _token.sequence) ||
            (result.status == JobStatus::Finished && deviceGeneration != _owner._gpuWork.DeviceGeneration()))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        if (outcome == HostOutcome::Succeeded)
        {
            const bool applied =
                _kind == UtilityReadbackKind::Merge
                    ? _prepared && _owner._session.ApplyPreparedMerge(_merge.value(), _prepared.value())
                    : _adjustment && _owner._session.AdjustmentEditingID() == _adjustment &&
                          _owner._session.BeginAdjustmentEditing(_adjustment.value(), image, true);
            if (!applied)
            {
                outcome = HostOutcome::Failed;
                error = E_FAIL;
            }
        }
        if (outcome != HostOutcome::Succeeded && _kind == UtilityReadbackKind::AdjustmentOpening &&
            _owner._session.AdjustmentEditingID() == _adjustment)
            _owner._session.SetAdjustmentEditingID(std::nullopt);
        if (outcome == HostOutcome::Failed)
        {
            if (_owner._session.TakeBudgetRefusal())
                _owner.ReportBudgetRefusal();
            else
                _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_FLATTEN);
        }
        _owner._utilityTicket.reset();
        _owner._utilityStamp.reset();
        (void)_owner._historyWork.Finish(_stamp);
        _owner.DocumentChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }
    Application& _owner; // owner stages only; Run/ProcessReadback never read host/session state
    const HistoryWorkStamp _stamp;
    const JobToken _token;
    const UtilityReadbackKind _kind;
    const std::optional<DocumentSession::MergePlan> _merge;
    const std::optional<Guid> _adjustment;
    std::optional<DocumentSession::PreparedMerge> _prepared;
    const std::shared_ptr<UtilityWorkTestGate> _gate;
    const DWORD _ownerThread;
};

HRESULT Application::PrepareUtilityDevice(HRESULT reason) noexcept
{
    if (_shutdownStarted || !_gpuWorkInitialized)
        return E_ABORT;
    RETURN_IF_FAILED(_renderer.RecoverDeviceForUtility(reason));
    BindCompositionDevice();
    return _gpuBindingError;
}

HRESULT Application::StartUtilityReadback(CanvasDocument document, UtilityReadbackKind kind,
                                          std::optional<DocumentSession::MergePlan> merge,
                                          std::optional<Guid> adjustment) noexcept
{
    if (_utilityTicket || !_session.Document())
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _utilityStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_utilityStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _utilityStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_utilityStamp)
        return E_UNEXPECTED;
    HRESULT result = E_INVALIDARG;
    try
    {
        auto token = _session.CaptureJobToken();
        if (token)
        {
            const D3D11_BOX region{
                0, 0, 0, static_cast<UINT>(token->canvasWidth), static_cast<UINT>(token->canvasHeight), 1};
            const auto bytes = DocumentReadbackJob::RequiredMemory(region);
            // The existing crop path holds its temporary and destination beside the full readback. Thumbnails
            // and bookkeeping have a bounded allowance. P1.9/P2 may reduce this conservative peak with adoption.
            const auto memory = kind == UtilityReadbackKind::Merge ? bytes * 3 + 64 * 1024 : bytes;
            if (bytes != 0)
            {
                std::unique_ptr<Job> job =
                    std::make_unique<UtilityReadbackJob>(*this, _utilityStamp.value(), std::move(token.value()),
                                                         std::move(document), kind, std::move(merge), adjustment);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _utilityTicket = ticket;
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        result = E_OUTOFMEMORY; // owning admission boundary: this request fails, no partial work is accepted
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_utilityStamp.value());
        _hostFinishedStamp = _utilityStamp;
        _utilityStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _utilityStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

int Application::RunUtilityReadbackSelfTest() noexcept
{
    enum class Mode
    {
        Ordered,
        Cancel,
        Failure,
        Stale,
        Rebind
    };
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt merge;
        bool visited = false, held = false, cancelPending = false;
        HRESULT rebound = E_PENDING;
    };
    const auto dispatch = [](void* context, void* native) noexcept
    {
        auto& state = *static_cast<State*>(context);
        auto& owner = state.owner;
        const auto& message = *static_cast<MSG*>(native);
        if (message.message != UtilityWorkTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(*static_cast<MSG*>(native));
            return;
        }
        state.visited = true;
        state.held = owner._historyWork.Busy() && owner._gpuWork.LentCount() == 1 &&
                     state.merge.Read().outcome == HostOutcome::Pending;
        if (state.mode == Mode::Cancel)
        {
            owner.CancelHostContinuations();
            state.cancelPending = state.merge.Read().outcome == HostOutcome::Pending;
        }
        else if (state.mode == Mode::Stale)
            owner._session.SetOpacity(0.5); // deliberate external mutation: a stale prepared merge must be dropped
        else if (state.mode == Mode::Rebind)
        {
            state.rebound = owner._renderer.RecoverDeviceForTest();
            owner.BindCompositionDevice();
        }
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._utilityWorkTestGate->release.get());
    };
    for (const auto mode : {Mode::Ordered, Mode::Cancel, Mode::Failure, Mode::Stale, Mode::Rebind})
    {
        _session.CreateDocument(16, 12);
        (void)InsertPaintedLayer(_session, L"Red", 8, 8, {0, 0}, 0, 0xFFFF0000);
        (void)InsertPaintedLayer(_session, L"Blue", 8, 8, {4, 0}, 0, 0xFF0000FF);
        _session.history.MarkSaved();
        DocumentChanged();
        const auto layers = _session.Document()->layers.size();
        const auto revision = _session.history.CurrentRevision();
        const auto presents = _renderer.PresentCount();
        _utilityWorkTestGate = std::make_shared<UtilityWorkTestGate>();
        _utilityWorkTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_utilityWorkTestGate->release)
            return 5;
        if (mode == Mode::Failure)
            _utilityWorkTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        State state{*this, mode, QueueHostInput(HostCommandInput{ChromeHost::kCommandMergeDown})};
        std::optional<ContinuationReceipt> invert;
        if (mode == Mode::Ordered)
            invert = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        if (SettleJobs(_jobs, 15000, dispatch, &state) != SettleResult::Settled)
        {
            SelfTestFailure(L"The utility readback harness did not settle.\n");
            return 5;
        }
        const auto outcome = state.merge.Read();
        const auto expected = mode == Mode::Cancel    ? HostOutcome::Cancelled
                              : mode == Mode::Failure ? HostOutcome::Failed
                              : mode == Mode::Stale   ? HostOutcome::Stale
                                                      : HostOutcome::Succeeded;
        bool correct = state.visited && state.held && outcome.outcome == expected && _gpuWork.LentCount() == 0 &&
                       _gpuWork.InFlightCount() == 0 && !_gpuWork.HasPending() && _jobs.ReservedMemoryBytes() == 0 &&
                       _composition.UtilitySurfaceBytes() == 0 && !_historyWork.Busy() && !_utilityTicket &&
                       _renderer.PresentCount() == presents;
        if (mode == Mode::Ordered)
        {
            const auto* active = _session.ActiveLayer();
            correct = correct && invert->Read().outcome == HostOutcome::Succeeded &&
                      _session.Document()->layers.size() == layers - 1 && _session.history.UndoName() == L"Invert" &&
                      active && active->asset && active->asset->image.Width() == 12 &&
                      active->asset->image.Height() == 8;
            if (active && active->asset)
            {
                const auto* bytes = active->asset->image.Data();
                correct = correct && bytes && bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 0 && bytes[3] == 255;
            }
            _session.Undo();
            correct = correct && _session.history.UndoName() == L"Merge Down";
        }
        else if (mode == Mode::Rebind)
            correct = correct && SUCCEEDED(state.rebound) && _utilityWorkTestGate->visits.load() == 2 &&
                      _session.Document()->layers.size() == layers - 1 && _session.history.UndoName() == L"Merge Down";
        else
        {
            correct = correct && _session.Document()->layers.size() == layers;
            if (mode == Mode::Stale)
                correct = correct && _session.ActiveLayer()->opacity == 0.5;
            else
                correct = correct && _session.history.CurrentRevision() == revision;
            if (mode == Mode::Cancel)
                correct = correct && state.cancelPending;
            if (mode == Mode::Failure)
                correct = correct && outcome.error == E_ACCESSDENIED;
        }
        const auto gateVisits = _utilityWorkTestGate->visits.load();
        _utilityWorkTestGate.reset();
        if (!correct)
        {
            std::fprintf(stderr, "Utility mode=%u visited=%u held=%u outcome=%u expected=%u error=0x%08x visits=%u\n",
                         static_cast<unsigned int>(mode), state.visited ? 1U : 0U, state.held ? 1U : 0U,
                         static_cast<unsigned int>(outcome.outcome), static_cast<unsigned int>(expected),
                         static_cast<unsigned int>(outcome.error), gateVisits);
            SelfTestFailure(L"Utility readback failed ordering, hidden ownership, exact drop or rebind validation.\n");
            return 5;
        }
    }
    return 0;
}
