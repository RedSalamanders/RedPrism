#include "Application.h"

#include "ChromeHost.h"
#include "ColorWorkTestGate.h"
#include "resource.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>
#include <variant>

// This first host bridge admits numeric Panel OK inputs only. Production entry points remain inline until the
// whole settlement/write/discard migration activates; the harness calls the same registered source and engine.
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
    if (_shutdownStarted || !_hostSourceRegistered)
    {
        return ContinuationReceipt::Failure(E_ABORT);
    }
    try
    {
        const auto ordinal = _historyWork.NextOrdinal();
        if (!ordinal)
        {
            return ContinuationReceipt::Failure(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
        }
        if (_hostAdmissionAllocationFailureForTest)
        {
            throw std::bad_alloc{}; // harness injection at the actual owning request construction boundary
        }
        HostRequest request = HostRequest::Create(HostPanelOkInput{opening});
        const auto receipt = request.Receipt();
        const auto admission = _hostRequests.Enqueue(_session.DocumentSerial(), ordinal.value(),
                                                     ContinuationKind::Command, request, RetireHostContinuation);
        if (admission != ContinuationAdmission::Queued)
        {
            (void)request.Finish(HostOutcome::Failed, admission == ContinuationAdmission::Full
                                                          ? HRESULT_FROM_WIN32(ERROR_BUSY)
                                                          : E_UNEXPECTED);
            return receipt;
        }
        if (!_jobs.RequestUiActionSource(_continuationAction))
        {
            // An initialized, registered owner cannot refuse except during stop. Retire every accepted input.
            (void)_hostRequests.Clear(RetireHostContinuation);
        }
        return receipt;
    }
    catch (const std::bad_alloc&)
    {
        // No request was admitted. Fail the entire construction without allocating a replacement result.
        return ContinuationReceipt::Failure(E_OUTOFMEMORY);
    }
}

void Application::FinishHostContinuation(ContinuationReceipt::Result result) noexcept
{
    if (!_activeHostRequest)
    {
        return;
    }
    (void)_activeHostRequest->input.Finish(result.outcome, result.error);
    _activeHostRequest.reset();
    _hostCommitStamp.reset();
    _hostPrerequisite = {HostOutcome::Pending, 0};
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
    const auto& active = _activeHostRequest.value();
    if (active.documentSerial != _session.DocumentSerial())
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    if (_hostPrerequisite.outcome != HostOutcome::Pending)
    {
        FinishHostContinuation(_hostPrerequisite);
        return;
    }
    const auto* panel = std::get_if<HostPanelOkInput>(&active.input.input);
    const auto& edit = _session.PendingColorEdit();
    if (!panel || !edit || panel->opening != edit->openingSequence)
    {
        FinishHostContinuation({HostOutcome::Stale, E_ABORT});
        return;
    }
    const auto outcome = StartColorCommit(active.ordinal);
    if (outcome == HostOutcome::Pending)
    {
        _hostCommitStamp = _colorCommitStamp;
        return;
    }
    FinishHostContinuation({outcome, _colorCommitError});
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
    if (_hostCommitStamp)
    {
        CancelColorCommit(); // keep the active receipt/input Pending until apply/drop acknowledges cancellation
    }
    else
    {
        // This bridge's panel request has no remaining stage after acknowledged apply/drop. A later Cancel
        // cannot relabel already applied pixels or replace a known failure. Multi-stage actions need their own
        // overall cancellation policy when the staged write/discard runner is introduced.
        const auto result = _activeHostRequest && _activeHostRequest->documentSerial != _session.DocumentSerial()
                                ? ContinuationReceipt::Result{HostOutcome::Stale, E_ABORT}
                            : _hostPrerequisite.outcome != HostOutcome::Pending
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
    if (!prepare() || !_hostSourceRegistered || !_jobs.IsIdle() || _hostRequests.Size() != 0 || _activeHostRequest)
    {
        return fail(L"The registered host source was not idle before request admission.\n");
    }
    const auto opening = _session.PendingColorEdit()->openingSequence;
    _hostAdmissionAllocationFailureForTest = true;
    const auto refusedAllocation = QueuePanelCommit(opening);
    _hostAdmissionAllocationFailureForTest = false;
    if (refusedAllocation.Read() != ContinuationReceipt::Result{HostOutcome::Failed, E_OUTOFMEMORY} ||
        _hostRequests.Size() != 0 || !_jobs.IsIdle() || !_session.HasColorEdit())
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
    if (!settled() || !_jobs.IsIdle() || _activeHostRequest || _hostRequests.Size() != 0)
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
    CancelColorEdit();
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
