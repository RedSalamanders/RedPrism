#include "Application.h"

#include "Tests/Support/Support.Tests.DocumentFixture.h"

#include "ChromeHost.h"
#include "HistoryRestoreWork.h"
#include "Runtime.Tests.HistoryRestoreTestGate.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <new>
#include <utility>

namespace
{
#pragma warning(push)
#pragma warning(disable : 4324)
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626)
struct RestorePayload final : DeferredReleaseNode
{
    HistoryRestoreAuthority authority;
    DocumentHistory::StateView history;
    std::optional<PreparedHistoryRestore> publication;
    std::shared_ptr<HistoryRestoreTestGate> gate;
    DWORD ownerThread = 0;
    RestorePayload() noexcept
    {
        bytes = kDeferredReleaseThreshold;
        destroy = [](DeferredReleaseNode* node) noexcept
        { const std::unique_ptr<RestorePayload> owned(static_cast<RestorePayload*>(node)); };
    }
    ~RestorePayload()
    {
        if (gate)
        {
            gate->retiredOffOwner.store(GetCurrentThreadId() != ownerThread);
            gate->retired.store(true);
        }
    }
};
#pragma warning(pop)

#pragma warning(pop)
struct RetireRestorePayload final
{
    void operator()(RestorePayload* payload) const noexcept
    {
        if (payload && !TryDeferRelease(*payload))
        {
            const std::unique_ptr<RestorePayload> owned(payload);
        }
    }
};
} // namespace

// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4625 4626 5026 5027)
class Application::HistoryRestoreJob final : public Job
{
  public:
    HistoryRestoreJob(Application& owner, HistoryWorkStamp stamp, bool redo, std::uint64_t retirement) noexcept
        : _owner(owner), _stamp(stamp), _redo(redo), _retirement(retirement),
          _authority(owner._session.CaptureHistoryRestoreAuthority()), _gate(owner._historyRestoreTestGate),
          _ownerThread(GetCurrentThreadId())
    {
    }
    JobResult Run(JobContext& context) override
    {
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload)
            return {};
        if (_refreshResidency)
        {
            if (_gate && _gate->throwResidencyRefresh)
                throw std::bad_alloc{};
            const bool refreshed =
                _payload->publication && _payload->publication->RefreshHistoryResidency(context.cancellation);
            return context.cancellation.IsCancelled() ? JobResult{JobStatus::Cancelled}
                   : refreshed                        ? JobResult{}
                                                      : JobResult{JobStatus::Failed, E_FAIL};
        }
        DocumentHistory::PrepareObserver observer;
        if (_gate && _gate->throwHistoryStage)
            observer = {[](void* opaque, DocumentHistory::PrepareStage stage)
                        {
                            auto& gate = *static_cast<HistoryRestoreTestGate*>(opaque);
                            if (gate.throwHistoryStage == stage)
                            {
                                gate.historyFaultHits.fetch_add(1);
                                throw std::bad_alloc{};
                            }
                        },
                        _gate.get()};
        _payload->publication = PrepareHistoryRestorePublication(_payload->authority, std::move(_payload->history),
                                                                 _redo, context.cancellation, observer);
        if (context.cancellation.IsCancelled())
            return {JobStatus::Cancelled};
        if (!_payload->publication)
            return {JobStatus::Failed, E_FAIL};
        if (_gate && _gate->lateResidencySource.HasStorage())
        {
            if (!_gate->lateResidencySource.Data())
                return {JobStatus::Failed, E_FAIL};
            _gate->residentGrowths.fetch_add(1);
        }
        if (_gate && _gate->hold)
        {
            _gate->visits.fetch_add(1);
            if (!PostThreadMessageW(_ownerThread, HistoryRestoreTestGate::kMessage, 0, 0))
                return {JobStatus::Failed, HRESULT_FROM_WIN32(GetLastError())};
            const HANDLE handles[]{_gate->release.get(), context.shutdownEvent};
            const auto waited = WaitForMultipleObjects(2, handles, FALSE, 10000);
            // Test-only witness: cancellation or a timeout alone cannot prove the shutdown event woke this Run.
            if (waited == WAIT_OBJECT_0 + 1)
                _gate->shutdownObserved.store(true);
            if (context.cancellation.IsCancelled() || waited == WAIT_OBJECT_0 + 1)
                return {JobStatus::Cancelled};
            return waited == WAIT_OBJECT_0 ? _gate->result
                                           : JobResult{JobStatus::Failed, HRESULT_FROM_WIN32(ERROR_TIMEOUT)};
        }
        return {};
    }
    bool DeferCompletion(JobResult result, JobTicket ticket) noexcept override
    {
        if (result.status != JobStatus::Finished || !Current() || _owner._historyWork.CancelRequested(_stamp))
            return false;
        if (_payload)
        {
            if (!_payload->publication)
                return false;
            const auto decision = _payload->publication->TryAcquireHistoryResidency(_residencyPermit);
            if (decision == DocumentHistory::ResidencyValidation::Ready)
                return false;
            if (decision == DocumentHistory::ResidencyValidation::Invalid)
            {
                _captureError = E_UNEXPECTED;
                return false;
            }
            _refreshResidency = true;
            if (_gate)
            {
                _gate->residencyRefreshes.fetch_add(1);
                _gate->refreshReservationHeld.store(_gate->reservationBytes != 0 &&
                                                    _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
            }
            if (_owner._jobs.Resume(ticket))
            {
                if (_gate && _gate->cancelResidencyRefresh)
                    (void)_owner._jobs.Cancel(ticket);
                return true;
            }
            _captureError = E_UNEXPECTED;
            return false;
        }
        try
        {
            _payload.reset(std::make_unique<RestorePayload>().release());
            _payload->bytes = (std::max)(_retirement, kDeferredReleaseThreshold);
            _payload->gate = _gate;
            _payload->ownerThread = _ownerThread;
            if (_gate)
            {
                _gate->captures.fetch_add(1);
                _gate->reservationHeld.store(_gate->reservationBytes != 0 &&
                                             _owner._jobs.ReservedMemoryBytes() >= _gate->reservationBytes);
                if (_gate->throwCaptureFailure)
                    throw std::bad_alloc{};
            }
            _payload->authority = _authority;
            _payload->history = _owner._session.history.CaptureState();
            if (_owner._jobs.Resume(ticket))
                return true;
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
        const auto releasePermit = wil::scope_exit([this]() noexcept { _residencyPermit.Reset(); });
        HostOutcome outcome = result.status == JobStatus::Cancelled ? HostOutcome::Cancelled
                              : result.status == JobStatus::Failed  ? HostOutcome::Failed
                                                                    : HostOutcome::Succeeded;
        std::int32_t error = result.error;
        if (!Current())
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        else if (_owner._historyWork.CancelRequested(_stamp))
        {
            outcome = HostOutcome::Cancelled;
            error = E_ABORT;
        }
        else if (_captureError && outcome == HostOutcome::Succeeded)
        {
            outcome = HostOutcome::Failed;
            error = _captureError;
        }
        else if (outcome == HostOutcome::Succeeded &&
                 (!_payload || !_payload->publication ||
                  !_owner._session.ApplyPreparedHistoryRestoreIfCurrent(_payload->publication.value())))
        {
            outcome = HostOutcome::Stale;
            error = E_ABORT;
        }
        _residencyPermit.Reset(); // Release before chrome, callbacks, or another history/source pump.
        if (outcome == HostOutcome::Failed)
            _owner._prompts->PostNotice(_owner._window.get(), kNoticeMessage, IDS_ERROR_HISTORY_RESTORE);
        _owner._historyRestoreTicket.reset();
        _owner._historyRestoreStamp.reset();
        if (outcome == HostOutcome::Succeeded && _owner._session.DocumentSerial() != _stamp.documentSerial)
        {
            if (_owner._activeHostRequest && _owner._hostCommitStamp == _stamp)
                _owner._activeHostRequest->documentSerial = _owner._session.DocumentSerial();
            (void)_owner._historyWork.FinishReplacement(_stamp, _owner._session.DocumentSerial());
        }
        else
            (void)_owner._historyWork.Finish(_stamp);
        _owner.DocumentChanged();
        _owner.ResumeHostHistory(_stamp, outcome, error);
    }

  private:
    [[nodiscard]] bool Current() const noexcept
    {
        return !_owner._shutdownStarted && _owner._historyRestoreStamp == _stamp &&
               _owner._historyWork.IsCurrent(_stamp) && _owner._session.CanApplyHistoryRestore(_authority);
    }
    Application& _owner;
    const HistoryWorkStamp _stamp;
    const bool _redo;
    const std::uint64_t _retirement;
    const HistoryRestoreAuthority _authority;
    const std::shared_ptr<HistoryRestoreTestGate> _gate;
    const DWORD _ownerThread;
    std::unique_ptr<RestorePayload, RetireRestorePayload> _payload;
    DocumentHistory::ResidencyPermit _residencyPermit;
    bool _refreshResidency = false;
    std::int32_t _captureError = 0;
};
#pragma warning(pop)

HRESULT Application::StartHistoryRestore(bool redo) noexcept
{
    if (_historyRestoreTicket)
        return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
    // Empty stacks and an open transaction have no restore effect. Preserve crop/redo without capturing history.
    if (redo ? !_session.history.CanRedo() : !_session.history.CanUndo())
        return S_OK;
    const auto ordinal = _activeHostRequest ? std::optional(_activeHostRequest->ordinal) : _historyWork.NextOrdinal();
    if (!ordinal)
        return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
    _historyRestoreStamp = _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), HistoryWorkKind::Commit);
    if (!_historyRestoreStamp && _hostFinishedStamp && _hostFinishedStamp->ordinal == ordinal.value())
        _historyRestoreStamp =
            _historyWork.Continue(_hostFinishedStamp.value(), _session.DocumentSerial(), HistoryWorkKind::Commit);
    if (!_historyRestoreStamp)
        return E_UNEXPECTED;
    auto memory = _session.history.RestoreMemoryBytes(_session.Document(), redo);
    auto retirement = _session.HistoryRestoreRetirementMemoryBytes();
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    HRESULT result = HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    // One prepared restore and one worker refresh candidate/index/stack may coexist under this ticket.
    if (memory > maximum / 2)
        memory = 0;
    else
        memory *= 2;
    if (memory && retirement && sizeof(HistoryRestoreJob) + sizeof(RestorePayload) <= maximum - memory)
    {
        memory += sizeof(HistoryRestoreJob) + sizeof(RestorePayload);
        if (memory <= maximum / 2 && memory * 2 <= maximum - retirement)
        {
            retirement += memory * 2;
            try
            {
                if (_historyRestoreTestGate)
                    _historyRestoreTestGate->reservationBytes = memory;
                std::unique_ptr<Job> job =
                    std::make_unique<HistoryRestoreJob>(*this, _historyRestoreStamp.value(), redo, retirement);
                JobTicket ticket;
                result = static_cast<HRESULT>(
                    _jobs.Submit(job, {JobClass::Commit, _session.DocumentSerial(), memory}, ticket));
                if (SUCCEEDED(result))
                    _historyRestoreTicket = ticket;
            }
            catch (const std::bad_alloc&)
            {
                result = E_OUTOFMEMORY;
            }
        }
    }
    if (FAILED(result))
    {
        (void)_historyWork.Finish(_historyRestoreStamp.value());
        _hostFinishedStamp = _historyRestoreStamp;
        _historyRestoreStamp.reset();
        return result;
    }
    if (_activeHostRequest)
    {
        _hostCommitStamp = _historyRestoreStamp;
        _hostAfterPrerequisite = HostStage::Complete;
    }
    return S_OK;
}

int Application::RunHistoryRestoreSelfTest() noexcept
{
    const auto originalHistoryBudget = _session.history.RetainedByteLimit();
    const auto restoreHistoryBudget =
        wil::scope_exit([this, originalHistoryBudget]() noexcept
                        { _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget}; });
    enum class Mode
    {
        Undo,
        Redo,
        Ordered,
        Empty,
        Crop,
        Mask,
        NewCanvasUndo,
        NewCanvasRedo,
        Cancel,
        Failure,
        CaptureFailure,
        UndoSnapshot,
        UndoStack,
        UndoTrim,
        RedoSnapshot,
        RedoStack,
        RedoTrim,
        Epoch,
        Generation,
        Active,
        MaskOnly,
        CropOnly,
        Lasso,
        Replacement,
        SavedDuringWorker,
        EmptyRedo,
        NestedUndo,
        NestedRedo,
        ResidencyUndo,
        ResidencyRedo,
        ResidencyUndoFault,
        ResidencyRedoFault,
        ResidencyUndoCancel,
        ResidencyRedoCancel
    };
// Existing identity/RAII/reference traits intentionally delete these implicit operations.
// Preserve aggregate and overload behavior. Owner: repository maintainers; review: 2027-01-31.
#pragma warning(push)
#pragma warning(disable : 4623 4626 5027)
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<CanvasDocument> document;
        std::optional<Guid> active;
        std::optional<CropRect> crop;
        Guid revision;
        Guid writtenRevision;
        bool mask = false, visited = false, held = false, cancelPending = false;
        int entries = 0;
    };
#pragma warning(pop)

    const auto dispatch = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message != HistoryRestoreTestGate::kMessage)
        {
            owner.DispatchQueuedMessage(message);
            return;
        }
        state.visited = true;
        state.held = owner._session.Document() == state.document && owner._historyWork.Busy() &&
                     owner._historyRestoreTicket && owner._jobs.ReservedMemoryBytes() != 0 &&
                     owner._session.history.CurrentRevision() == state.revision &&
                     owner._session.history.UndoCount() == state.entries &&
                     state.receipt.Read().outcome == HostOutcome::Pending && owner._gpuWork.LentCount() == 0;
        switch (state.mode)
        {
        case Mode::Cancel:
            owner.CancelHostContinuations();
            state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy();
            break;
        case Mode::Epoch:
            owner._session.history.Begin(L"Epoch only", owner._session.Document(), owner._session.ActiveLayerID());
            owner._session.history.End(owner._session.Document(), owner._session.ActiveLayerID());
            break;
        case Mode::Generation:
            owner._session.EditForTest([](CanvasDocument& document) { document.resolution = 300; });
            break;
        case Mode::Active:
            owner._session.SelectLayer(owner._session.Document()->layers.front().id);
            break;
        case Mode::MaskOnly:
            owner._session.SelectLayerTarget(owner._session.ActiveLayerID().value(), false);
            break;
        case Mode::CropOnly:
            owner._session.SetCropFrame(CropRect{0, 0, 1, 1});
            break;
        case Mode::Lasso:
            owner._session.BeginLasso({0, 0}, SelectionMode::New);
            break;
        case Mode::Replacement:
            owner._session.CreateDocument(3, 2, true);
            break;
        case Mode::SavedDuringWorker:
            // A save of the live revision during worker preparation is independent of authority.
            owner._session.history.MarkSaved(state.writtenRevision);
            break;
        case Mode::CaptureFailure:
        case Mode::Crop:
        case Mode::Empty:
        case Mode::EmptyRedo:
        case Mode::Failure:
        case Mode::Mask:
        case Mode::NestedRedo:
        case Mode::NestedUndo:
        case Mode::NewCanvasRedo:
        case Mode::NewCanvasUndo:
        case Mode::Ordered:
        case Mode::Redo:
        case Mode::RedoSnapshot:
        case Mode::RedoStack:
        case Mode::RedoTrim:
        case Mode::ResidencyRedo:
        case Mode::ResidencyRedoCancel:
        case Mode::ResidencyRedoFault:
        case Mode::ResidencyUndo:
        case Mode::ResidencyUndoCancel:
        case Mode::ResidencyUndoFault:
        case Mode::Undo:
        case Mode::UndoSnapshot:
        case Mode::UndoStack:
        case Mode::UndoTrim:
        default:
            break;
        }
        state.document = owner._session.Document();
        state.active = owner._session.ActiveLayerID();
        state.crop = owner._session.CropFrame();
        state.mask = owner._session.IsMaskSelected();
        state.revision = owner._session.history.CurrentRevision();
        state.entries = owner._session.history.UndoCount();
        owner._composition.ReleaseDocumentSurfaces();
        state.held = state.held && owner._composition.CanvasSurfaceBytes() == 0;
        (void)SetEvent(owner._historyRestoreTestGate->release.get());
    };
    for (const auto mode : {Mode::Undo,
                            Mode::Redo,
                            Mode::Ordered,
                            Mode::Empty,
                            Mode::Crop,
                            Mode::Mask,
                            Mode::NewCanvasUndo,
                            Mode::NewCanvasRedo,
                            Mode::Cancel,
                            Mode::Failure,
                            Mode::CaptureFailure,
                            Mode::UndoSnapshot,
                            Mode::UndoStack,
                            Mode::UndoTrim,
                            Mode::RedoSnapshot,
                            Mode::RedoStack,
                            Mode::RedoTrim,
                            Mode::Epoch,
                            Mode::Generation,
                            Mode::Active,
                            Mode::MaskOnly,
                            Mode::CropOnly,
                            Mode::Lasso,
                            Mode::Replacement,
                            Mode::SavedDuringWorker,
                            Mode::EmptyRedo,
                            Mode::NestedUndo,
                            Mode::NestedRedo,
                            Mode::ResidencyUndo,
                            Mode::ResidencyRedo,
                            Mode::ResidencyUndoFault,
                            Mode::ResidencyRedoFault,
                            Mode::ResidencyUndoCancel,
                            Mode::ResidencyRedoCancel})
    {
        const bool residency = mode >= Mode::ResidencyUndo && mode <= Mode::ResidencyRedoCancel;
        const bool residencyRedo =
            mode == Mode::ResidencyRedo || mode == Mode::ResidencyRedoFault || mode == Mode::ResidencyRedoCancel;
        const bool residencyFault = mode == Mode::ResidencyUndoFault || mode == Mode::ResidencyRedoFault;
        const bool residencyCancel = mode == Mode::ResidencyUndoCancel || mode == Mode::ResidencyRedoCancel;
        Raster lateResidencySource;
        const bool newCanvas = mode == Mode::NewCanvasUndo || mode == Mode::NewCanvasRedo;
        const bool noPublication =
            mode == Mode::Empty || mode == Mode::EmptyRedo || mode == Mode::NestedUndo || mode == Mode::NestedRedo;
        const bool redo = residencyRedo || mode == Mode::Redo || mode == Mode::NewCanvasRedo ||
                          mode == Mode::EmptyRedo || mode == Mode::NestedRedo ||
                          (mode >= Mode::RedoSnapshot && mode <= Mode::RedoTrim);
        _session.history = DocumentHistory{kHistoryEntryLimit, originalHistoryBudget};
        _session.ClearDocument();
        const Guid emptyRevision = _session.history.CurrentRevision();
        _session.CreateDocument(1, 1, true);
        Guid undoRevision = emptyRevision;
        Guid redoRevision = _session.history.CurrentRevision();
        std::optional<Guid> restoredActive = _session.ActiveLayerID();
        if (!newCanvas)
        {
            if (mode == Mode::Active)
                _session.AddBlankLayer();
            constexpr std::array<std::uint8_t, 4> pixel{17, 34, 51, 255};
            const Raster image = Raster::Make(1, 1, RasterFormat::Bgra8888, pixel.data(), pixel.size());
            const Guid id = _session.ActiveLayerID().value();
            EditLayerRecordsForTest(
                _session,
                [&](DocumentFixture& document)
                {
                    for (auto& layer : document.layers)
                        if (layer.id == id)
                        {
                            layer.asset = RasterAsset{image, Raster::MakeThumbnail(image), layer.name};
                            if (mode == Mode::Mask || mode == Mode::MaskOnly)
                            {
                                constexpr std::array<std::uint8_t, 1> maskPixel{173};
                                const Raster mask = Raster::Make(1, 1, RasterFormat::Gray8, maskPixel.data(), 1);
                                LayerMask placed;
                                placed.asset = RasterAsset{mask, Raster::MakeThumbnail(mask), L"Mask"};
                                layer.mask = std::move(placed);
                            }
                        }
                });
            _session.history.Reset();
            undoRevision = _session.history.CurrentRevision();
            _session.SetOpacity(0.5);
            _session.SetOpacity(0.25);
            redoRevision = _session.history.CurrentRevision();
            _session.Undo(); // reference fixture construction leaves both stacks populated
            if (mode == Mode::Empty || mode == Mode::EmptyRedo)
                _session.history.Reset();
            if (mode == Mode::Mask || mode == Mode::MaskOnly)
                _session.SelectLayerTarget(id, true);
        }
        else if (redo)
            _session.Undo(); // production Redo must be admissible with no live document
        if (residency)
        {
            constexpr std::array<std::uint8_t, 4> historicalBytes{61, 62, 63, 255};
            const Raster leaf =
                Raster::Make(1, 1, RasterFormat::Bgra8888, historicalBytes.data(), historicalBytes.size());
            lateResidencySource = Raster::MakeSparse(2, 1, RasterFormat::Bgra8888, {}, 0, 0, {{0, 0, leaf}}, 1);
            auto historical = DocumentFixture::Copy(_session.Document().value());
            historical.layers.front().asset->image = lateResidencySource;
            _session.history = DocumentHistory{100, 8};
            auto baseline = DocumentHistory::PrepareEdit(_session.history.CaptureState(), std::move(historical).Seal(),
                                                         _session.ActiveLayerID(), _session.Document(),
                                                         _session.ActiveLayerID(), L"Literal historical cache");
            if (!baseline || !_session.history.Publish(baseline.value()))
                return 5;
            baseline.reset();
            undoRevision = _session.history.CurrentRevision();
            _session.SetOpacity(0.25);
            redoRevision = _session.history.CurrentRevision();
            if (redo)
                _session.Undo();
            if (_session.history.RetainedBytes(_session.Document()) != 4 || lateResidencySource.IsMaterialized())
                return 5;
        }
        if (mode == Mode::NestedUndo || mode == Mode::NestedRedo)
            _session.history.Begin(L"Open transaction", _session.Document(), _session.ActiveLayerID());
        if (mode == Mode::Crop || mode == Mode::Empty || mode == Mode::EmptyRedo)
            _session.SetCropFrame(CropRect{0, 0, 1, 1});
        _session.history.MarkSaved();
        DocumentChanged();
        const auto presents = _renderer.PresentCount();
        _historyRestoreTestGate = std::make_shared<HistoryRestoreTestGate>();
        _historyRestoreTestGate->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!_historyRestoreTestGate->release)
            return 5;
        const bool historyFailure = mode >= Mode::UndoSnapshot && mode <= Mode::RedoTrim;
        _historyRestoreTestGate->throwCaptureFailure = mode == Mode::CaptureFailure;
        _historyRestoreTestGate->lateResidencySource = lateResidencySource;
        _historyRestoreTestGate->throwResidencyRefresh = residencyFault;
        _historyRestoreTestGate->cancelResidencyRefresh = residencyCancel;
        if (historyFailure)
        {
            constexpr std::array stages{DocumentHistory::PrepareStage::RestoreSnapshot,
                                        DocumentHistory::PrepareStage::StackGrowth,
                                        DocumentHistory::PrepareStage::TrimIndex};
            _historyRestoreTestGate->throwHistoryStage =
                stages[(static_cast<unsigned int>(mode) - static_cast<unsigned int>(Mode::UndoSnapshot)) % 3];
        }
        if (mode == Mode::Failure)
            _historyRestoreTestGate->result = {JobStatus::Failed, E_ACCESSDENIED};
        State state{*this,
                    mode,
                    QueueHostInput(HostCommandInput{redo ? ChromeHost::kCommandRedo : ChromeHost::kCommandUndo}),
                    _session.Document(),
                    _session.ActiveLayerID(),
                    _session.CropFrame(),
                    _session.history.CurrentRevision(),
                    redoRevision,
                    _session.IsMaskSelected()};
        state.entries = _session.history.UndoCount();
        std::optional<ContinuationReceipt> younger;
        if (mode == Mode::Ordered)
            younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandInvert});
        else if (mode == Mode::NewCanvasUndo)
            younger = QueueHostInput(HostCommandInput{ChromeHost::kCommandRedo});
        const auto settled = SettleJobs(_jobs, 15000, dispatch, &state);
        const auto actual = state.receipt.Read();
        const auto expected =
            mode == Mode::Cancel || residencyCancel ? HostOutcome::Cancelled
            : mode == Mode::Failure || mode == Mode::CaptureFailure || historyFailure || residencyFault
                ? HostOutcome::Failed
            : mode >= Mode::Epoch && mode <= Mode::Replacement ? HostOutcome::Stale
                                                               : HostOutcome::Succeeded;
        const auto gate = _historyRestoreTestGate;
        const bool reached = noPublication || mode == Mode::CaptureFailure || historyFailure
                                 ? !state.visited
                                 : state.visited && state.held;
        bool correct =
            settled == SettleResult::Settled && reached && actual.outcome == expected && !_historyWork.Busy() &&
            !_historyRestoreTicket && !_historyRestoreStamp && _jobs.ReservedMemoryBytes() == 0 &&
            _renderer.PresentCount() == presents &&
            (noPublication ? gate->reservationBytes == 0 && !gate->retired.load()
                           : gate->reservationHeld.load() && gate->retired.load() && gate->retiredOffOwner.load());
        if (mode == Mode::CaptureFailure || historyFailure || residencyFault)
            correct = correct && actual.error == E_OUTOFMEMORY;
        if (historyFailure)
            correct = correct && gate->historyFaultHits.load() == 1;
        if (mode == Mode::Cancel)
            correct = correct && state.cancelPending;
        if (expected != HostOutcome::Succeeded || noPublication)
            correct = correct && _session.Document() == state.document && _session.ActiveLayerID() == state.active &&
                      _session.CropFrame() == state.crop && _session.IsMaskSelected() == state.mask &&
                      _session.history.CurrentRevision() == state.revision &&
                      _session.history.UndoCount() == state.entries;
        if (expected == HostOutcome::Succeeded && !noPublication)
        {
            if (newCanvas)
            {
                correct = correct && _session.Document().has_value() == redo &&
                          _session.history.CurrentRevision() == (redo ? redoRevision : emptyRevision) &&
                          _session.history.UndoCount() == (redo ? 1 : 0) && _session.history.CanRedo() != redo;
                if (redo)
                    correct = correct && _session.ActiveLayerID() == restoredActive && _session.Document()->width == 1;
                else
                    correct = correct && !_session.ActiveLayerID() && younger->Read().outcome == HostOutcome::Stale;
            }
            else
            {
                std::array<std::uint8_t, 4> pixel{};
                if (!_session.ActiveLayer() || !_session.ActiveLayer()->asset)
                    correct = false;
                else
                {
                    _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 1, 1, pixel.data(), 4, 0);
                    correct = correct && _session.ActiveLayer()->opacity ==
                                             (residency ? (redo ? 0.25 : 0.5) : (redo ? 0.25 : 1.0));
                }
                if (mode == Mode::Ordered)
                    correct = correct && younger->Read().outcome == HostOutcome::Succeeded &&
                              pixel == std::array<std::uint8_t, 4>{238, 221, 204, 255} &&
                              _session.history.UndoCount() == 1 && _session.history.UndoName() == L"Invert" &&
                              !_session.history.CanRedo();
                else
                    correct = correct && pixel == std::array<std::uint8_t, 4>{17, 34, 51, 255} &&
                              _session.history.CurrentRevision() == (redo ? redoRevision : undoRevision) &&
                              _session.history.UndoCount() == (redo ? (residency ? 1 : 2) : 0) &&
                              _session.history.CanRedo() != redo;
                if (mode == Mode::Mask)
                    correct = correct && _session.IsMaskSelected() && _session.ActiveLayer()->mask.has_value();
                if (mode == Mode::Crop)
                    correct = correct && !_session.CropFrame();
                if (mode == Mode::SavedDuringWorker)
                    correct = correct && _session.history.IsModified(); // later saved live revision is retained
            }
        }
        if (residency)
        {
            correct = correct && gate->captures.load() == 1 && gate->visits.load() == 1 &&
                      gate->residentGrowths.load() == 1 && gate->residencyRefreshes.load() == 1 &&
                      gate->refreshReservationHeld.load() && lateResidencySource.ByteSize() == 8;
            if (expected == HostOutcome::Succeeded)
                correct = correct && _session.history.RetainedBytes(_session.Document()) == 0 &&
                          _session.history.CurrentRevision() == (redo ? redoRevision : undoRevision) &&
                          _session.history.IsModified(); // Refresh must preserve the saved revision of the live input.
            else
                correct = correct && !_session.history.IsModified();
        }
        _historyRestoreTestGate.reset();
        _session.CancelLasso();
        if (mode == Mode::NestedUndo || mode == Mode::NestedRedo)
            _session.history.End(_session.Document(), _session.ActiveLayerID());
        if (!correct)
        {
            std::fprintf(
                stderr,
                "FAIL history-restore-host.mode=%u outcome=%u expected=%u settled=%u visited=%u held=%u error=%08X\n",
                static_cast<unsigned int>(mode), static_cast<unsigned int>(actual.outcome),
                static_cast<unsigned int>(expected), static_cast<unsigned int>(settled), state.visited, state.held,
                static_cast<unsigned int>(actual.error));
            return 5;
        }
        std::fprintf(stderr, "PASS history-restore-host.mode=%u\n", static_cast<unsigned int>(mode));
    }
    return 0;
}
