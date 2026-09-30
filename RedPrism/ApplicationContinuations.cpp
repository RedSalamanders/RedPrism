#include "Application.h"

#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "FileDialogs.h"
#include "ImageCodecs.h"
#include "ProjectStore.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <variant>

// The source owns the selected input across prompts, settlement, writes and dependent actions. Each callback
// only admits event-time arguments. Pixel/IO snapshots are acquired after preceding history work has finished.
void Application::ContinuationAction::Run() noexcept
{
    _owner.RunHostContinuation();
}

bool Application::InitializeHostContinuations() noexcept
{
    if (_hostSourceRegistered || !_jobs.RegisterUiActionSource(_continuationAction))
    {
        return false;
    }
    _hostSourceRegistered = true;
    return true;
}

void Application::RetireHostContinuation(HostRequestQueue::Entry&& entry, ContinuationRetirement reason) noexcept
{
    entry.input.Retire(reason);
}

ContinuationReceipt Application::QueuePanelCommit(std::uint64_t opening) noexcept
{
    const auto receipt = QueueHostInput(HostPanelOkInput{opening});
    if (receipt.Read().outcome == HostOutcome::Pending)
    {
        _pendingPanelOpening = opening;
        SyncAdjustPanel();
        _frameInvalidated = true;
    }
    return receipt;
}

ContinuationReceipt Application::QueueHostInput(HostRequestInput input) noexcept
{
    if (_shutdownStarted || !_hostSourceRegistered)
    {
        return ContinuationReceipt::Failure(E_ABORT);
    }
    try
    {
        auto owned = HostRequest::TryCreate(input, _hostInputBudget, _hostAdmissionAllocationFailureForTest);
        if (!owned)
        {
            return ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
        }
        return QueueHostRequest(std::move(owned.value()));
    }
    catch (const std::bad_alloc&)
    {
        // No request was admitted. Fail the entire construction without allocating a replacement result.
        return ContinuationReceipt::Failure(E_OUTOFMEMORY);
    }
}

ContinuationReceipt Application::QueueHostRequest(HostRequest request) noexcept
{
    const auto receipt = request.Receipt();
    const auto ordinal = _historyWork.NextOrdinal();
    if (_shutdownStarted || !_hostSourceRegistered || !ordinal)
    {
        (void)request.Finish(HostOutcome::Failed, !ordinal ? HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW) : E_ABORT);
        return receipt;
    }
    const auto admission = _hostRequests.Enqueue(_session.DocumentSerial(), ordinal.value(), ContinuationKind::Command,
                                                 request, RetireHostContinuation);
    if (admission != ContinuationAdmission::Queued)
    {
        (void)request.Finish(HostOutcome::Failed,
                             admission == ContinuationAdmission::Full ? HRESULT_FROM_WIN32(ERROR_BUSY) : E_UNEXPECTED);
    }
    else if (!_jobs.RequestUiActionSource(_continuationAction))
    {
        (void)_hostRequests.Clear(RetireHostContinuation);
    }
    return receipt;
}

ContinuationReceipt Application::QueueHostPaths(std::span<const std::wstring_view> paths, HostPathAction action,
                                                std::optional<Vec2> point) noexcept
{
    try
    {
        auto owned = HostRequest::TryCopyPaths(paths, action, point, _hostInputBudget);
        return owned ? QueueHostRequest(std::move(owned.value()))
                     : ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA));
    }
    catch (const std::bad_alloc&)
    {
        return ContinuationReceipt::Failure(E_OUTOFMEMORY);
    }
}

ContinuationReceipt Application::QueueHostWrite(HostWriteFormat format, bool saveAs) noexcept
{
    return QueueHostInput(HostWriteInput{format, saveAs, std::nullopt});
}

void Application::ReportHostAdmission(const ContinuationReceipt& receipt) noexcept
{
    if (receipt.Read().outcome == HostOutcome::Failed)
    {
        _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_BUSY);
    }
}

void Application::FinishHostContinuation(ContinuationReceipt::Result result) noexcept
{
    if (!_activeHostRequest)
    {
        return;
    }
    if (const auto* ending = std::get_if<HostEndSessionInput>(&_activeHostRequest->input.Input());
        ending && ending->generation == _endSessionGeneration)
    {
        _endSessionPending = false;
        _endSessionPromptDeferred = false;
        if (_window)
            (void)ShutdownBlockReasonDestroy(_window.get());
    }
    (void)_activeHostRequest->input.Finish(result.outcome, result.error);
    _activeHostRequest.reset();
    _pendingPanelOpening.reset();
    _hostCommitStamp.reset();
    _hostPrerequisite = {HostOutcome::Pending, 0};
    _hostStage = HostStage::Begin;
    _hostPathIndex = 0;
    _hostSawProject = false;
    _hostImportNewProject = false;
    _hostSaveBeforeAction = false;
    _hostBatchFailed = false;
    if (!_shutdownStarted && _hostRequests.Size() != 0)
    {
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::RunHostContinuation() noexcept
{
    if (_shutdownStarted)
    {
        return;
    }
    if (_hostCommitStamp || _historyWork.Busy())
    {
        return; // consumed readiness stays disarmed until this prerequisite's exact UI completion
    }
    if (!_activeHostRequest)
    {
        auto taken = _hostRequests.TakeReady(_session.DocumentSerial(), false, RetireHostContinuation);
        if (taken.stale != 0)
        {
            _prompts->PostNotice(_window.get(), kNoticeMessage, IDS_STATUS_QUEUED_STALE);
        }
        _activeHostRequest = std::move(taken.ready);
        if (!_activeHostRequest)
        {
            return;
        }
    }
    auto& active = _activeHostRequest.value();
    if (active.documentSerial != _session.DocumentSerial())
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (const auto* ending = std::get_if<HostEndSessionInput>(&active.input.Input());
        ending && (!_endSessionPending || ending->generation != _endSessionGeneration))
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (_hostPrerequisite.outcome != HostOutcome::Pending)
    {
        if (_hostPrerequisite.outcome != HostOutcome::Succeeded)
        {
            FinishHostContinuation(_hostPrerequisite);
            return;
        }
        _hostPrerequisite = {HostOutcome::Pending, 0};
        _hostStage = _hostSaveBeforeAction || std::holds_alternative<HostWriteInput>(active.input.Input())
                         ? HostStage::Write
                         : _hostAfterPrerequisite;
    }
    // One source dispatch advances a bounded number of fixed stages. Each project in a batch returns to the outer
    // pump so errors/prompts and a younger Windows cancellation cannot destroy owners inside a callback stack.
    for (int transitions = 0; transitions < 8 && _activeHostRequest; ++transitions)
    {
        const auto& input = active.input.Input();
        const auto* paths = std::get_if<HostPathsInput>(&input);
        switch (_hostStage)
        {
        case HostStage::Begin:
            _hostAfterPrerequisite = HostStage::Execute;
            if (const auto* panel = std::get_if<HostPanelOkInput>(&input))
            {
                const auto& edit = _session.PendingColorEdit();
                if (!edit || panel->opening != edit->openingSequence)
                {
                    FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                    return;
                }
                _hostStage = HostStage::Settle;
            }
            else if (const auto* write = std::get_if<HostWriteInput>(&input))
            {
                _hostWriteFormat = write->format;
                _hostStage = HostStage::Output;
            }
            else if (paths)
            {
                _hostStage =
                    paths->action == HostPathAction::OpenPaths ? HostStage::PathProject : HostStage::PathImages;
            }
            else if (std::holds_alternative<HostNewInput>(input) ||
                     std::holds_alternative<HostCloseProjectInput>(input) ||
                     std::holds_alternative<HostCloseWindowInput>(input) ||
                     std::holds_alternative<HostEndSessionInput>(input) ||
                     (std::get_if<HostSizeApplyInput>(&input) &&
                      std::get<HostSizeApplyInput>(input).draft.kind == SizeCardKind::NewCanvas))
            {
                _hostStage = HostStage::Confirm;
            }
            else if (const auto* command = std::get_if<HostCommandInput>(&input);
                     command && (command->command == ChromeHost::kCommandCanvasSize ||
                                 command->command == ChromeHost::kCommandImageSize ||
                                 command->command == ChromeHost::kCommandNewCanvas))
            {
                _hostStage = HostStage::Execute; // opening these cards deliberately dismisses the live panel
            }
            else
            {
                _hostStage = HostStage::Settle;
            }
            break;
        case HostStage::PathProject:
            while (
                _hostPathIndex < paths->paths.size() &&
                (paths->paths[_hostPathIndex].empty() || !LooksLikeProjectPath(paths->paths[_hostPathIndex].c_str())))
            {
                ++_hostPathIndex;
            }
            if (_hostPathIndex == paths->paths.size())
            {
                _hostStage = HostStage::PathImages;
            }
            else
            {
                _hostSawProject = true;
                _hostAfterPrerequisite = HostStage::Execute;
                _hostStage = HostStage::Confirm;
            }
            break;
        case HostStage::PathImages:
        {
            std::size_t count = 0;
            const std::wstring* first = nullptr;
            for (const auto& path : paths->paths)
            {
                if (!path.empty() &&
                    (paths->action != HostPathAction::OpenPaths || !LooksLikeProjectPath(path.c_str())))
                {
                    if (!first)
                        first = &path;
                    ++count;
                }
            }
            if (!first)
            {
                FinishHostContinuation({_hostBatchFailed ? HostOutcome::Failed : HostOutcome::Succeeded,
                                        _hostBatchFailed ? E_FAIL : S_OK});
                return;
            }
            const auto choice = paths->action == HostPathAction::OpenPaths ? PromptImageOpen(FileTitle(*first), count)
                                : paths->action == HostPathAction::ImagesAsProject ? ImageOpenChoice::NewProject
                                                                                   : ImageOpenChoice::AddLayer;
            if (choice == ImageOpenChoice::Cancel)
            {
                FinishHostContinuation({HostOutcome::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)});
                return;
            }
            _hostImportNewProject = choice == ImageOpenChoice::NewProject || !_session.Document();
            _hostPathIndex = paths->paths.size(); // execute the image group, rather than another project
            _hostStage = _hostImportNewProject ? HostStage::Confirm : HostStage::Settle;
            break;
        }
        case HostStage::Confirm:
        {
            const auto answer = AskDiscardChanges();
            // A modal prompt can receive WM_ENDSESSION(FALSE); its answer belongs only to the same generation.
            if (const auto* ending = std::get_if<HostEndSessionInput>(&input);
                ending && (!_endSessionPending || ending->generation != _endSessionGeneration))
            {
                FinishHostContinuation({HostOutcome::Stale, E_ABORT});
                return;
            }
            if (answer == Prompts::Answer::Cancel)
            {
                FinishHostContinuation({HostOutcome::Cancelled, HRESULT_FROM_WIN32(ERROR_CANCELLED)});
                return;
            }
            _hostSaveBeforeAction = answer == Prompts::Answer::First;
            _hostWriteFormat = HostWriteFormat::Project;
            _hostStage = _hostSaveBeforeAction ? HostStage::Output : HostStage::Execute;
            break;
        }
        case HostStage::Output:
        {
            const auto* write = std::get_if<HostWriteInput>(&input);
            const auto result = SelectHostOutput(_hostWriteFormat, write && write->saveAs);
            if (FAILED(result))
            {
                FinishHostContinuation(
                    {result == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? HostOutcome::Cancelled : HostOutcome::Failed,
                     result});
                return;
            }
            _hostStage = HostStage::Settle;
            break;
        }
        case HostStage::Settle:
        {
            const auto outcome = StartColorCommit(active.ordinal);
            if (outcome == HostOutcome::Pending)
            {
                _hostCommitStamp = _colorCommitStamp;
                return;
            }
            if (outcome != HostOutcome::Succeeded)
            {
                FinishHostContinuation({outcome, _colorCommitError});
                return;
            }
            _hostStage = _hostSaveBeforeAction || std::holds_alternative<HostWriteInput>(input)
                             ? HostStage::Write
                             : _hostAfterPrerequisite;
            break;
        }
        case HostStage::Write:
        {
            const auto result = WriteHostOutput(_hostWriteFormat);
            if (FAILED(result))
            {
                FinishHostContinuation({HostOutcome::Failed, result});
                return;
            }
            if (!_hostSaveBeforeAction)
            {
                FinishHostContinuation({HostOutcome::Succeeded, S_OK});
                return;
            }
            _hostSaveBeforeAction = false;
            _hostStage = _hostAfterPrerequisite;
            break;
        }
        case HostStage::Execute:
            if (!ExecuteHostInput())
            {
                FinishHostContinuation({HostOutcome::Failed, E_FAIL});
                return;
            }
            if (paths && _hostPathIndex < paths->paths.size())
            {
                ++_hostPathIndex;
                active.documentSerial = _session.DocumentSerial(); // only this batch can rebind after replacement
                _hostStage = HostStage::PathProject;
                (void)_jobs.RequestUiActionSource(_continuationAction);
                return;
            }
            FinishHostContinuation(
                {_hostBatchFailed ? HostOutcome::Failed : HostOutcome::Succeeded, _hostBatchFailed ? E_FAIL : S_OK});
            return;
        }
    }
    if (_activeHostRequest)
    {
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::ResumeHostHistory(HistoryWorkStamp stamp, HostOutcome outcome, std::int32_t error) noexcept
{
    if (_shutdownStarted || !_hostSourceRegistered)
    {
        return;
    }
    if (_activeHostRequest && _hostCommitStamp == stamp)
    {
        _hostCommitStamp.reset();
        _hostPrerequisite = {outcome, error};
    }
    if (!_hostCommitStamp && (_activeHostRequest || _hostRequests.Size() != 0))
    {
        // A queued request may have disarmed behind any earlier history operation. Its finisher notifies here
        // after apply/drop and HistoryWork::Finish; operation migrations must wire this same hook.
        (void)_jobs.RequestUiActionSource(_continuationAction);
    }
}

void Application::CancelHostContinuations() noexcept
{
    (void)_hostRequests.Clear(RetireHostContinuation);
    _pendingPanelOpening.reset();
    if (_hostCommitStamp)
    {
        CancelColorCommit(); // keep the active receipt/input Pending until apply/drop acknowledges cancellation
    }
    else
    {
        // A panel's acknowledged pixels retain their result. Cancel suppresses any remaining write/replacement
        // stage of a multi-stage input, even if its prerequisite has already applied successfully.
        const auto result = _activeHostRequest && _activeHostRequest->documentSerial != _session.DocumentSerial()
                                ? ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT}
                            : _activeHostRequest &&
                                    std::holds_alternative<HostPanelOkInput>(_activeHostRequest->input.Input()) &&
                                    _hostPrerequisite.outcome != HostOutcome::Pending
                                ? _hostPrerequisite
                                : ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT};
        FinishHostContinuation(result);
    }
}

void Application::StopHostContinuations() noexcept
{
    if (_hostSourceRegistered)
    {
        (void)_jobs.UnregisterUiActionSource(_continuationAction);
        _hostSourceRegistered = false;
    }
    (void)_hostRequests.Clear(RetireHostContinuation);
    FinishHostContinuation({HostOutcome::Cancelled, E_ABORT});
}

namespace
{
class CountContinuationAction final : public UiAction
{
  public:
    explicit CountContinuationAction(unsigned int& count) noexcept : _count(count) {}
    void Run() noexcept override
    {
        ++_count;
    }

  private:
    unsigned int& _count;
};
} // namespace

int Application::RunHostContinuationSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settled = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    unsigned int ordinaryRuns = 0; // queued actions borrow this through cleanup's final settle on every exit
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            _hostAdmissionAllocationFailureForTest = false;
            if (_colorWorkTestGate)
            {
                (void)SetEvent(_colorWorkTestGate->release.get());
            }
            CancelHostContinuations();
            CancelColorEdit();
            if (!settled())
            {
                SelfTestFailure(L"Host continuation work did not join at its failure boundary.\n");
                std::_Exit(5);
            }
            _colorWorkTestGate.reset();
        });
    constexpr std::array<std::uint8_t, 16> bytes{0, 0, 0, 255, 64, 64, 64, 255, 128, 128, 128, 255, 255, 255, 255, 255};
    const Raster source = Raster::Make(4, 1, RasterFormat::Bgra8888, bytes.data(), 16);
    const auto prepare = [&]()
    {
        CancelHostContinuations();
        CancelColorEdit();
        if (!settled())
        {
            return false;
        }
        _colorWorkTestGate.reset();
        CanvasDocument document;
        document.id = Guid::Generate();
        document.width = 4;
        document.height = 1;
        ImageLayer layer = ImageLayer::Blank(L"Owned host commit", Vec2{4, 1});
        const Guid active = layer.id;
        layer.asset = RasterAsset{source, source, layer.name};
        document.layers.push_back(std::move(layer));
        ReplaceDocument(std::move(document), active, {});
        if (!BeginColorEdit(ChromeHost::kCommandLevels))
        {
            return false;
        }
        OnAdjustAction(AdjustAction::SetInputGamma, 2);
        return settled();
    };
    const auto gate = [this]()
    {
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = ColorWorkTestKind::Commit;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    if (!prepare() || !_hostSourceRegistered || !_jobs.IsIdle() || _hostRequests.Size() != 0 || _activeHostRequest ||
        _hostInputBudget.Used() != 0)
    {
        return fail(L"The registered host source was not idle before request admission.\n");
    }
    const auto opening = _session.PendingColorEdit()->openingSequence;
    auto fullBudget = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
    const auto refusedBudget = QueuePanelCommit(opening);
    if (!fullBudget ||
        refusedBudget.Read() !=
            ContinuationReceipt::Result{HostOutcome::Failed, HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)} ||
        _hostRequests.Size() != 0 || !_jobs.IsIdle() || !_session.HasColorEdit() ||
        _hostInputBudget.Used() != HostInputBudget::kStorageLimit)
    {
        return fail(L"The host byte budget did not refuse admission without mutation or an unowned receipt.\n");
    }
    fullBudget.reset();
    if (_hostInputBudget.Used() != 0)
    {
        return fail(L"The host byte budget did not return its released reservation.\n");
    }
    std::printf("PASS continuation-host.byte-budget-refusal-and-release\n");
    _hostAdmissionAllocationFailureForTest = true;
    const auto refusedAllocation = QueuePanelCommit(opening);
    _hostAdmissionAllocationFailureForTest = false;
    if (refusedAllocation.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
        _hostRequests.Size() != 0 || !_jobs.IsIdle() || !_session.HasColorEdit() || _hostInputBudget.Used() != 0)
    {
        return fail(L"The host owning construction boundary did not fail allocation without admission.\n");
    }
    std::array<ContinuationReceipt, HostRequestQueue::kCapacity> queued;
    for (auto& receipt : queued)
    {
        receipt = QueuePanelCommit(opening);
        if (receipt.Read().outcome != HostOutcome::Pending)
        {
            return fail(L"A numeric host request was not admitted Pending.\n");
        }
    }
    const auto ninth = QueuePanelCommit(opening);
    if (ninth.Read() != ContinuationReceipt::Result{HostOutcome::Failed, HRESULT_FROM_WIN32(ERROR_BUSY)} ||
        _hostRequests.Size() != HostRequestQueue::kCapacity || _session.history.UndoCount() != 0)
    {
        return fail(L"The ninth host request did not fail locally without history work.\n");
    }
    CancelHostContinuations();
    for (const auto& receipt : queued)
    {
        if (receipt.Read().outcome != HostOutcome::Cancelled)
        {
            return fail(L"Host Clear stranded an accepted receipt Pending.\n");
        }
    }
    if (!settled() || !_jobs.IsIdle() || _activeHostRequest || _hostRequests.Size() != 0 ||
        _hostInputBudget.Used() != 0)
    {
        return fail(L"An empty cleared host source kept waking.\n");
    }
    std::printf("PASS continuation-host.admission-full-allocation-clear-and-idle\n");

    if (!gate())
    {
        return fail(L"Could not create the actual host commit gate.\n");
    }
    for (std::size_t i = 0; i < JobScheduler::kUiActionCapacity; ++i)
    {
        std::unique_ptr<UiAction> action = std::make_unique<CountContinuationAction>(ordinaryRuns);
        if (!_jobs.PostUiAction(action))
        {
            return fail(L"Could not fill the ordinary host action queue.\n");
        }
    }
    const auto committed = QueuePanelCommit(opening);
    for (int pass = 0; pass < 2 && !ColorCommitBusy(); ++pass)
    {
        (void)RunUiActions(_jobs);
    }
    if (!ColorCommitBusy() || !_activeHostRequest || !_hostCommitStamp || ordinaryRuns > 1 ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0 ||
        committed.Read().outcome != HostOutcome::Pending || _session.ActiveLayer()->asset->image != source)
    {
        return fail(L"A full ordinary action queue blocked host admission or prematurely completed its receipt.\n");
    }
    const auto activeStamp = _hostCommitStamp.value();
    for (std::size_t i = ordinaryRuns; i < JobScheduler::kUiActionCapacity; ++i)
    {
        if (!RunUiActions(_jobs))
        {
            return fail(L"The waiting host source starved an ordinary action.\n");
        }
    }
    (void)DrainCompletions(_jobs);
    if (ordinaryRuns != JobScheduler::kUiActionCapacity || RunUiActions(_jobs) || _jobs.HasUiWork())
    {
        return fail(L"The waiting host source rearmed without a completion.\n");
    }
    HistoryWorkStamp wrongStamp = activeStamp;
    ++wrongStamp.acquisition;
    ResumeHostHistory(wrongStamp, HostOutcome::Succeeded, 0);
    if (_jobs.HasUiWork() || _hostCommitStamp != activeStamp || committed.Read().outcome != HostOutcome::Pending)
    {
        return fail(L"A mismatched host acquisition resumed the selected request.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    // This harness-only wait observes completion application without running its dependent UI action.
    const ULONGLONG completionDeadline = GetTickCount64() + 10000;
    while (ColorCommitBusy())
    {
        const ULONGLONG now = GetTickCount64();
        if (now >= completionDeadline ||
            WaitForSingleObject(_jobs.WakeHandle(), static_cast<DWORD>(completionDeadline - now)) != WAIT_OBJECT_0)
        {
            return fail(L"The host commit did not signal UI completion.\n");
        }
        (void)DrainCompletions(_jobs);
    }
    if (_historyWork.Busy() || !_jobs.HasUiWork() || committed.Read().outcome != HostOutcome::Pending ||
        _session.HasColorEdit() || _session.history.UndoCount() != 1 || !RunUiActions(_jobs) ||
        committed.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0} || _activeHostRequest)
    {
        return fail(L"The host did not publish success on a later UI action after apply/drop.\n");
    }
    std::array<std::uint8_t, 16> actual{};
    _session.ActiveLayer()->asset->image.ReadRegion(0, 0, 4, 1, actual.data(), 16, 0);
    constexpr std::array<std::uint8_t, 16> expected{0,   0,   0,   255, 128, 128, 128, 255,
                                                    181, 181, 181, 255, 255, 255, 255, 255};
    if (actual != expected || !settled() || !_jobs.IsIdle())
    {
        return fail(L"The resumed host result differed from the independent Gamma 2 pixels or did not become idle.\n");
    }
    std::printf("PASS continuation-host.full-action-queue-wait-exact-stamp-apply-and-receipt\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare cancellation of an active host request.\n");
    }
    const auto cancelled = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The cancelled host request did not reach the worker gate.\n");
    }
    CancelHostContinuations();
    if (cancelled.Read().outcome != HostOutcome::Pending || !_historyWork.Busy() || !_activeHostRequest)
    {
        return fail(L"Host cancellation released ownership or its barrier before worker acknowledgement.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || cancelled.Read() != ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} ||
        _historyWork.Busy() || _activeHostRequest || _session.ActiveLayer()->asset->image != source ||
        _session.history.UndoCount() != 0 || !_session.HasColorEdit())
    {
        return fail(L"Cancelled host work changed pixels/history or stranded its observer.\n");
    }
    std::printf("PASS continuation-host.cancel-retains-input-through-acknowledged-drop\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare the throwing host request.\n");
    }
    _colorWorkTestGate->throwAllocationFailure = true;
    const auto failed = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy())
    {
        return fail(L"The throwing host request did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || failed.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
        _historyWork.Busy() || _activeHostRequest || !_session.HasColorEdit() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"The scheduler's allocation failure was lost by the host receipt or changed surviving state.\n");
    }
    std::printf("PASS continuation-host.actual-runner-allocation-error-survives-publication\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare an earlier independent color barrier.\n");
    }
    const auto earlierOrdinal = _historyWork.NextOrdinal();
    if (!earlierOrdinal || StartColorCommit(earlierOrdinal.value()) != HostOutcome::Pending ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The earlier independent commit did not start.\n");
    }
    const auto behindEarlier = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || _activeHostRequest || _hostRequests.Size() != 1 ||
        behindEarlier.Read().outcome != HostOutcome::Pending || !_historyWork.Busy())
    {
        return fail(L"A queued request selected a snapshot before an earlier independent barrier finished.\n");
    }
    (void)DrainCompletions(_jobs);
    if (RunUiActions(_jobs) || _jobs.HasUiWork())
    {
        return fail(L"A queued request polled an unchanged earlier barrier.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!settled() || behindEarlier.Read().outcome != HostOutcome::Stale || _activeHostRequest ||
        _hostRequests.Size() != 0 || _historyWork.Busy() || _session.history.UndoCount() != 1)
    {
        return fail(L"An earlier independently completed commit lost the queued request's wake-up.\n");
    }
    std::printf("PASS continuation-host.queued-input-resumes-after-independent-history\n");

    constexpr std::array<HistoryWorkKind, 4> laterKinds{HistoryWorkKind::Selection, HistoryWorkKind::Stroke,
                                                        HistoryWorkKind::Open, HistoryWorkKind::Import};
    for (const auto kind : laterKinds)
    {
        if (!prepare())
        {
            return fail(L"Could not prepare the non-color history completion notification.\n");
        }
        const auto ordinal = _historyWork.NextOrdinal();
        const auto barrier =
            ordinal ? _historyWork.Begin(_session.DocumentSerial(), ordinal.value(), kind) : std::nullopt;
        if (!barrier)
        {
            return fail(L"Could not acquire an independent non-color barrier.\n");
        }
        const auto behindOtherKind = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
        if (!RunUiActions(_jobs) || _activeHostRequest || RunUiActions(_jobs) || _jobs.HasUiWork() ||
            behindOtherKind.Read().outcome != HostOutcome::Pending)
        {
            return fail(L"A host input selected or polled behind another history kind.\n");
        }
        // Finisher seam only; real selection/stroke/open/import jobs migrate in their own P1 slices.
        if (!_historyWork.Finish(barrier.value()))
        {
            return fail(L"The non-color finisher could not release its exact barrier.\n");
        }
        ResumeHostHistory(barrier.value(), HostOutcome::Succeeded, 0);
        if (!_jobs.HasUiWork() || !settled() || behindOtherKind.Read().outcome != HostOutcome::Succeeded ||
            _hostRequests.Size() != 0 || _activeHostRequest || _historyWork.Busy() ||
            _session.history.UndoCount() != 1 || !_jobs.IsIdle())
        {
            return fail(L"The common non-color completion hook lost a waiting owned request.\n");
        }
    }
    std::printf("PASS continuation-host.all-history-kind-completion-hooks\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare late successful host cancellation.\n");
    }
    const auto lateCancelled = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy())
    {
        return fail(L"The late-cancelled host commit did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The host commit did not compute before late cancellation.\n");
    }
    CancelHostContinuations();
    if (lateCancelled.Read().outcome != HostOutcome::Pending || !_historyWork.Busy() || !settled() ||
        lateCancelled.Read() != ContinuationReceipt::Result{HostOutcome::Cancelled, E_ABORT} ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"Late successful worker pixels crossed the host's acknowledged cancellation.\n");
    }
    std::printf("PASS continuation-host.late-success-drops-before-cancelled-publication\n");

    for (const bool failWorker : std::array<bool, 2>{false, true})
    {
        if (!prepare() || !gate())
        {
            return fail(L"Could not prepare cancellation after acknowledged terminal work.\n");
        }
        _colorWorkTestGate->throwAllocationFailure = failWorker;
        const auto acknowledged = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
        if (!RunUiActions(_jobs) || !ColorCommitBusy())
        {
            return fail(L"The acknowledged terminal host fixture did not start.\n");
        }
        (void)SetEvent(_colorWorkTestGate->release.get());
        const ULONGLONG terminalDeadline = GetTickCount64() + 10000;
        while (ColorCommitBusy())
        {
            const ULONGLONG now = GetTickCount64();
            if (now >= terminalDeadline ||
                WaitForSingleObject(_jobs.WakeHandle(), static_cast<DWORD>(terminalDeadline - now)) != WAIT_OBJECT_0)
            {
                return fail(L"The terminal host work did not acknowledge apply/drop.\n");
            }
            (void)DrainCompletions(_jobs);
        }
        const ContinuationReceipt::Result terminal =
            failWorker ? ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY}
                       : ContinuationReceipt::Result{HostOutcome::Succeeded, 0};
        if (acknowledged.Read().outcome != HostOutcome::Pending || _hostPrerequisite != terminal ||
            !_activeHostRequest || _historyWork.Busy())
        {
            return fail(L"The host receipt did not retain the acknowledged result before deferred publication.\n");
        }
        CancelHostContinuations();
        if (acknowledged.Read() != terminal || _activeHostRequest ||
            _session.history.UndoCount() != (failWorker ? 0 : 1) ||
            (failWorker && (_session.ActiveLayer()->asset->image != source || !_session.HasColorEdit())) ||
            (!failWorker && _session.HasColorEdit()) || !settled() || !_jobs.IsIdle() ||
            acknowledged.Read() != terminal)
        {
            return fail(L"Cancel after apply/drop relabelled an acknowledged outcome or changed its effect.\n");
        }
    }
    std::printf("PASS continuation-host.cancel-after-acknowledgement-preserves-terminal-outcome\n");

    if (!prepare() || !gate())
    {
        return fail(L"Could not prepare active host document replacement.\n");
    }
    const auto activeStale = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy())
    {
        return fail(L"The active stale host commit did not start.\n");
    }
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (WaitForSingleObject(_colorWorkTestGate->computed.get(), 10000) != WAIT_OBJECT_0)
    {
        return fail(L"The stale host commit did not compute before replacement.\n");
    }
    const CanvasDocument activeReplacement = _session.Document().value();
    const auto activeReplacementLayer = _session.ActiveLayerID();
    ReplaceDocument(activeReplacement, activeReplacementLayer, {});
    if (!settled() || activeStale.Read().outcome != HostOutcome::Stale || _activeHostRequest || _historyWork.Busy() ||
        _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != 0)
    {
        return fail(L"An active host commit applied to a replacement with reused persisted IDs.\n");
    }
    std::printf("PASS continuation-host.active-document-replacement-drops-stale-pixels\n");

    if (!prepare())
    {
        return fail(L"Could not prepare the stale panel host request.\n");
    }
    const auto stalePanel = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    // External lifetime invalidation is distinct from the UI's explicit Cancel, which retires the queue.
    _session.CancelColorEdit();
    if (!BeginColorEdit(ChromeHost::kCommandLevels) || !settled() || stalePanel.Read().outcome != HostOutcome::Stale ||
        !_session.HasColorEdit() || _session.history.UndoCount() != 0)
    {
        return fail(L"An older queued panel OK settled a reopened panel.\n");
    }
    const auto staleDocument = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    const CanvasDocument replacement = _session.Document().value(); // deliberately retain persisted document/layer IDs
    const auto replacementActive = _session.ActiveLayerID();
    ReplaceDocument(replacement, replacementActive, {});
    if (!settled() || staleDocument.Read().outcome != HostOutcome::Stale || _activeHostRequest ||
        _session.history.UndoCount() != 0)
    {
        return fail(L"A queued host request crossed a replacement with reused persisted IDs.\n");
    }
    std::printf("PASS continuation-host.queued-panel-and-document-lifetimes\n");

    CancelColorEdit();
    if (!settled())
    {
        return fail(L"Could not settle the metadata host fixture.\n");
    }
    _session.AddAdjustment(AdjustmentKind::Levels);
    const Guid adjustment = _session.ActiveLayerID().value();
    _session.SetAdjustmentEditingID(adjustment);
    if (!_session.BeginAdjustmentEditing(adjustment, source, true))
    {
        return fail(L"Could not open the immediate adjustment metadata request.\n");
    }
    DocumentChanged();
    OnAdjustAction(AdjustAction::SetInputGamma, 2);
    if (!settled())
    {
        return fail(L"Could not settle metadata previews before host admission.\n");
    }
    const int beforeMetadataHistory = _session.history.UndoCount();
    const auto metadata = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (metadata.Read().outcome != HostOutcome::Pending || !RunUiActions(_jobs) ||
        metadata.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0} || _activeHostRequest ||
        _hostCommitStamp || _historyWork.Busy() || _session.HasColorEdit() ||
        _session.history.UndoCount() != beforeMetadataHistory + 1 ||
        _session.ActiveLayer()->adjustment->levels.ranges[0].gamma != 2 ||
        _session.Document()->layers.front().asset->image != source || !settled() || !_jobs.IsIdle() ||
        metadata.Read() != ContinuationReceipt::Result{HostOutcome::Succeeded, 0})
    {
        return fail(L"Immediate host metadata settlement lost its receipt, history or idle state.\n");
    }
    std::printf("PASS continuation-host.immediate-metadata-settlement-and-terminal-receipt\n");

    // A separate owner can close without ending this harness. Its source is ready, but shutdown cannot run it.
    auto closing = std::make_unique<Application>(_instance, true, nullptr, 0);
    const auto closeCleanup = wil::scope_exit([&]() noexcept { closing->Shutdown(); });
    if (FAILED(closing->_jobs.Initialize(1)) || !closing->InitializeHostContinuations())
    {
        return fail(L"Could not prepare a separate host continuation shutdown owner.\n");
    }
    const auto closed = closing->QueuePanelCommit(1);
    closing->Shutdown();
    const auto afterClose = closing->QueuePanelCommit(2);
    if (closed.Read().outcome != HostOutcome::Cancelled ||
        afterClose.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_ABORT} ||
        closing->_hostSourceRegistered || !closing->_jobs.IsIdle() || closing->_activeHostRequest ||
        closing->_hostRequests.Size() != 0)
    {
        return fail(L"Host shutdown did not unregister readiness and retire each retained observer.\n");
    }
    std::printf("PASS continuation-host.shutdown-unregister-retirement-and-refusal\n");
    return 0;
}

int Application::RunHostFlowSelfTest() noexcept
{
    const auto fail = [](const wchar_t* message) noexcept
    {
        SelfTestFailure(message);
        return 5;
    };
    const auto settle = [this]() noexcept
    { return SettleJobs(_jobs, 10000, DispatchMessageThunk, this) == SettleResult::Settled; };
    wchar_t temporary[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, temporary))
        return fail(L"Host flow temporary directory unavailable.\n");
    const auto prefix = std::wstring(temporary) + L"RedPrism.host-flow-" + std::to_wstring(GetCurrentProcessId());
    const auto project = prefix + L"-source.prism";
    const auto imagePath = prefix + L"-source.png";
    const auto output = prefix + L"-output.prism";
    const auto cleanup = wil::scope_exit(
        [&]() noexcept
        {
            if (_colorWorkTestGate)
                (void)SetEvent(_colorWorkTestGate->release.get());
            CancelHostContinuations();
            CancelColorEdit();
            _colorWorkTestGate.reset();
            _forcedDiscardAnswer.reset();
            _endSessionPending = false;
            _closeConfirmed = false;
            if (_window)
                (void)ShutdownBlockReasonDestroy(_window.get());
            if (!settle())
                std::_Exit(5);
            (void)DeleteFileW(project.c_str());
            (void)DeleteFileW(imagePath.c_str());
            (void)DeleteFileW(output.c_str());
        });
    const auto source = SolidRaster(4, 1, 0xFF404040u);
    CanvasDocument fixture;
    fixture.id = Guid::Generate();
    fixture.width = 4;
    fixture.height = 1;
    ImageLayer layer = ImageLayer::Blank(L"Host flow", Vec2{4, 1});
    const auto id = layer.id;
    layer.asset = RasterAsset{source, source, layer.name};
    fixture.layers.push_back(std::move(layer));
    ImageLayer other = ImageLayer::Blank(L"Other host target", Vec2{4, 1});
    const auto otherId = other.id;
    other.asset = RasterAsset{source, source, other.name};
    other.visible = false;
    fixture.layers.push_back(std::move(other));
    if (SaveProject(project.c_str(), fixture, id, true) != ProjectIoStatus::Ok ||
        ExportDocumentPng(source, 72, imagePath.c_str()) != ProjectIoStatus::Ok)
        return fail(L"Host flow fixtures failed to encode.\n");
    const auto prepare = [&](bool failed) noexcept
    {
        CancelColorEdit();
        if (!settle())
            return false;
        _colorWorkTestGate.reset();
        ReplaceDocument(fixture, id, output);
        if (failed)
            _session.SelectAll();
        if (!BeginColorEdit(failed ? ChromeHost::kCommandContentAwareFill : ChromeHost::kCommandLevels))
            return false;
        if (!failed)
            OnAdjustAction(AdjustAction::SetInputGamma, 2);
        if (!settle())
            return false;
        _forcedDiscardAnswer = Prompts::Answer::First;
        _colorWorkTestGate = std::make_shared<ColorWorkTestGate>();
        _colorWorkTestGate->kind = ColorWorkTestKind::Commit;
        return _colorWorkTestGate->entered && _colorWorkTestGate->release && _colorWorkTestGate->computed;
    };
    const auto pumpReceipt = [&](const ContinuationReceipt& receipt) noexcept
    {
        for (int step = 0; step < 64 && receipt.Read().outcome == HostOutcome::Pending; ++step)
        {
            (void)DrainCompletions(_jobs);
            (void)RunUiActions(_jobs);
            // The actual close producer posts a callback-safe close. Observe and remove it before it can end the
            // parent harness; ordinary application pumping dispatches this message after the source returns.
            MSG close{};
            while (PeekMessageW(&close, _window.get(), WM_CLOSE, WM_CLOSE, PM_REMOVE))
            {
            }
            if (receipt.Read().outcome == HostOutcome::Pending && !_jobs.HasUiWork() &&
                WaitForSingleObject(_jobs.WakeHandle(), 10000) != WAIT_OBJECT_0)
                return false;
        }
        return receipt.Read().outcome != HostOutcome::Pending;
    };
    constexpr std::array<const char*, 21> names{"save",         "png",           "jpeg",
                                                "new",          "close-project", "close-window",
                                                "end-session",  "open-project",  "image-new-project",
                                                "image-layers", "canvas-size",   "image-size",
                                                "new-canvas",   "tool",          "select-layer",
                                                "target-layer", "invoke-layer",  "reorder-layer",
                                                "rename-layer", "opacity",       "command"};
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        for (int mode = 0; mode < 4; ++mode)
        {
            const bool failure = mode == 0;
            (void)DeleteFileW(output.c_str());
            if (!prepare(failure))
                return fail(L"Could not prepare a staged host caller.\n");
            const auto serial = _session.DocumentSerial();
            const auto history = _session.history.UndoCount();
            HostRequestInput input = HostCommandInput{ChromeHost::kCommandNewLayer};
            switch (index)
            {
            case 0:
                input = HostWriteInput{HostWriteFormat::Project, false, output};
                break;
            case 1:
                input = HostWriteInput{HostWriteFormat::Png, false, output};
                break;
            case 2:
                input = HostWriteInput{HostWriteFormat::Jpeg, false, output};
                break;
            case 3:
                input = HostNewInput{};
                break;
            case 4:
                input = HostCloseProjectInput{};
                break;
            case 5:
                input = HostCloseWindowInput{};
                break;
            case 6:
                _endSessionPending = true;
                ++_endSessionGeneration;
                input = HostEndSessionInput{_endSessionGeneration, 0};
                break;
            case 7:
                input = HostPathsInput{{project}, HostPathAction::OpenPaths, {}};
                break;
            case 8:
                input = HostPathsInput{{imagePath}, HostPathAction::ImagesAsProject, {}};
                break;
            case 9:
                input = HostPathsInput{{imagePath}, HostPathAction::ImagesAsLayers, Vec2{2, .5}};
                break;
            case 10:
            case 11:
            case 12:
                _sizeDialog.kind = index == 10   ? SizeCardKind::CanvasSize
                                   : index == 11 ? SizeCardKind::ImageSize
                                                 : SizeCardKind::NewCanvas;
                _sizeDialog.canvas = CanvasSizeDraft(4, 1, 72);
                _sizeDialog.canvas.Set(8, true);
                _sizeDialog.image = ImageSizeDraft(4, 1, 72);
                _sizeDialog.image.Set(8, true);
                _sizeDialog.newWidth = 8;
                _sizeDialog.newHeight = 2;
                ++_sizeInvocation;
                input = HostSizeApplyInput{_sizeDialog, _sizeInvocation};
                break;
            case 13:
                input = HostToolInput{EditorTool::Hand};
                break;
            case 14:
                input = HostLayerSelectInput{otherId};
                break;
            case 15:
                input = HostLayerTargetInput{otherId, false, false};
                break;
            case 16:
                input = HostLayerInvokeInput{otherId};
                break;
            case 17:
                input = HostLayerReorderInput{otherId, id, {}, DxUi::TreeDropPlace::After};
                break;
            case 18:
                input = HostLayerRenameInput{id, L"Owned rename"};
                break;
            case 19:
                input = HostLayerOpacityInput{id, .5, ChromeEditPhase::Commit};
                break;
            }
            const auto receipt = QueueHostInput(std::move(input));
            for (int start = 0; start < 4 && !ColorCommitBusy(); ++start)
                (void)RunUiActions(_jobs);
            if (!ColorCommitBusy() || receipt.Read().outcome != HostOutcome::Pending ||
                WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0 ||
                _session.DocumentSerial() != serial || _session.ActiveLayer()->asset->image != source ||
                GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
                return fail(L"A dependent host action ran before its commit acknowledgement.\n");
            OnCommand(ChromeHost::kCommandZoomActual);
            OnTransformAction(TransformAction::FlipHorizontal, 1);
            OnSelectionAction(SelectionAction::SetMode, 2);
            OnGradientAction(GradientAction::Apply, 0);
            OnShapeAction(ShapeAction::SetKind, 1);
            OnCropAction(CropAction::Apply, 0);
            if (_session.Document()->layers.front() != fixture.layers.front() ||
                _session.history.UndoCount() != history)
                return fail(L"A header callback mutated the document beneath admitted history work.\n");
            if (!_window || !_historyWork.Busy())
                return fail(L"View input did not survive a pending host flow.\n");
            if (mode == 2)
                CancelHostContinuations();
            if (mode == 3)
            {
                if (index == 6)
                    OnEndSession(false);
                else
                    ReplaceDocument(fixture, id, {}); // reuse persisted document and layer IDs
            }
            (void)SetEvent(_colorWorkTestGate->release.get());
            if (!pumpReceipt(receipt))
                return fail(L"A staged host receipt did not terminate.\n");
            _closeConfirmed = false;
            if (mode == 0 || mode == 2)
            {
                if (receipt.Read().outcome != (mode == 0 ? HostOutcome::Failed : HostOutcome::Cancelled) ||
                    _session.DocumentSerial() != serial || !_session.HasColorEdit() ||
                    _session.ActiveLayer()->asset->image != source || _session.history.UndoCount() != history ||
                    GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return fail(L"Failed/cancelled settlement authorized its dependent action or lost the panel.\n");
            }
            else if (mode == 3)
            {
                if (receipt.Read().outcome != HostOutcome::Stale ||
                    GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES)
                    return fail(L"A stale document/session-end flow authorized a write or dependent action.\n");
            }
            else
            {
                if (receipt.Read().outcome != HostOutcome::Succeeded || _session.HasColorEdit())
                    return fail(L"A successful host settlement failed to execute its dependent action.\n");
                if ((index >= 14 && index <= 16 && _session.ActiveLayerID() != otherId) ||
                    (index == 16 && (!_chrome->RenameCardOpen() || _renameLayer != otherId)) ||
                    (index == 17 && _session.Document()->layers.front().id != otherId) ||
                    (index == 18 && _session.ActiveLayer()->name != L"Owned rename") ||
                    (index == 19 && _session.ActiveLayer()->opacity != .5) ||
                    (index == 20 && _session.Document()->layers.size() != 3))
                {
                    SelfTestFailure(
                        std::format(L"Successful host caller '{}' did not apply its retained arguments.\n", index));
                    return 5;
                }
                if (index < 3)
                {
                    if (GetFileAttributesW(output.c_str()) == INVALID_FILE_ATTRIBUTES)
                        return fail(L"A successful write receipt preceded actual output.\n");
                    if (index == 0)
                    {
                        const auto saved = LoadProject(output.c_str());
                        if (saved.status != ProjectIoStatus::Ok ||
                            saved.document.layers.front().asset->image.Data()[0] != 128)
                            return fail(L"Save captured pixels before successful gamma settlement.\n");
                    }
                }
            }
            if (!settle() || _historyWork.Busy() || _activeHostRequest || _hostInputBudget.Used())
                return fail(L"A completed staged host flow retained work, input or its byte reservation.\n");
            std::printf("PASS host-flow.%s.%s\n", names[index],
                        mode == 0   ? "failed-fill"
                        : mode == 1 ? "success"
                        : mode == 2 ? "cancel"
                                    : "stale");
        }
    }
    if (!prepare(false))
        return fail(L"Could not prepare Shape release behind a color commit.\n");
    _session.shapeKind = ShapeKind::Rectangle;
    _session.shapeCornerRadius = 0;
    _session.SetForegroundColor(0xFFFF0000u);
    _session.BeginShape(Vec2{0, 0});
    const auto color = QueuePanelCommit(_session.PendingColorEdit()->openingSequence);
    if (!RunUiActions(_jobs) || !ColorCommitBusy() ||
        WaitForSingleObject(_colorWorkTestGate->entered.get(), 10000) != WAIT_OBJECT_0)
        return fail(L"Shape release did not reach its preceding color gate.\n");
    DxUi::PointerEvent release{};
    release.action = DxUi::PointerAction::Up;
    if (!DragPointer(release, _viewport.ViewFromDocument(Vec2{2, 1}, DocumentSize())) || _session.PendingShape() ||
        _session.Document()->layers.size() != 2 || _session.history.UndoCount() != 0 || _hostRequests.Size() != 1)
        return fail(L"Shape release recorded history beneath a pending color commit.\n");
    _session.SetForegroundColor(0xFF00FF00u); // execution must use the event-time red, not this later palette value
    (void)SetEvent(_colorWorkTestGate->release.get());
    if (!pumpReceipt(color) || !settle() || _session.Document()->layers.size() != 3 ||
        _session.history.UndoCount() != 2 || _session.ActiveLayer()->asset->image.Width() != 2 ||
        _session.ActiveLayer()->asset->image.Data()[2] != 255 || _session.ActiveLayer()->asset->image.Data()[1] != 0 ||
        _hostInputBudget.Used())
        return fail(L"Shape release lost its captured draft/color or ordered history.\n");
    std::printf("PASS host-flow.shape-release-after-commit-with-owned-color\n");
    return 0;
}
