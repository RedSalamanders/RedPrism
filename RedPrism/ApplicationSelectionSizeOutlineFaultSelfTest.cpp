#include "Application.h"

#include "ChromeHost.h"
#include "SelectionMoveCloseTestGate.h"
#include "SizingWorkTestGate.h"

#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace
{
DocumentSelection SizeOutlineSquare(double left, double top)
{
    return DocumentSelection{false, SelectionLoops{SelectionLoop{Vec2{left, top}, Vec2{left + 20, top},
                                                                 Vec2{left + 20, top + 20}, Vec2{left, top + 20}}}};
}
class OccupySizeOutlineSlot final : public Job
{
  public:
    JobResult Run(JobContext&) override
    {
        return {};
    }
    void Completed(JobResult) noexcept override {}
};
} // namespace

int Application::RunSelectionSizeOutlineFaultSelfTest() noexcept
{
    enum class Mode
    {
        Canvas,
        Image,
        Foreground,
        Background,
        Ordered,
        Pump,
        CloseCancel,
        CloseFailure,
        CloseCapture,
        SizeCancel,
        SizeFailure,
        SizeCapture,
        SizeHistory,
        Opening,
        Card,
        Generation,
        HeldCoexisting,
        SelectedCoexisting,
        SelectedClosed,
        SelectedOpening,
        SelectedCard,
        Saved
    };
    struct Case final
    {
        const char* name;
        Mode mode;
        std::optional<DocumentHistory::PrepareStage> stage;
    };
    constexpr std::array cases{
        Case{"canvas", Mode::Canvas, {}},
        Case{"image", Mode::Image, {}},
        Case{"foreground", Mode::Foreground, {}},
        Case{"background", Mode::Background, {}},
        Case{"ordered-undo", Mode::Ordered, {}},
        Case{"pump", Mode::Pump, {}},
        Case{"close-cancel", Mode::CloseCancel, {}},
        Case{"close-failure", Mode::CloseFailure, {}},
        Case{"close-capture", Mode::CloseCapture, {}},
        Case{"size-cancel", Mode::SizeCancel, {}},
        Case{"size-failure", Mode::SizeFailure, {}},
        Case{"size-capture", Mode::SizeCapture, {}},
        Case{"size-before-snapshot", Mode::SizeHistory, DocumentHistory::PrepareStage::BeforeSnapshot},
        Case{"size-after-snapshot", Mode::SizeHistory, DocumentHistory::PrepareStage::AfterSnapshot},
        Case{"size-entry", Mode::SizeHistory, DocumentHistory::PrepareStage::Entry},
        Case{"size-stack-growth", Mode::SizeHistory, DocumentHistory::PrepareStage::StackGrowth},
        Case{"size-trim-index", Mode::SizeHistory, DocumentHistory::PrepareStage::TrimIndex},
        Case{"held-opening", Mode::Opening, {}},
        Case{"held-card", Mode::Card, {}},
        Case{"held-generation", Mode::Generation, {}},
        Case{"held-coexisting", Mode::HeldCoexisting, {}},
        Case{"selected-coexisting", Mode::SelectedCoexisting, {}},
        Case{"selected-closed", Mode::SelectedClosed, {}},
        Case{"selected-opening", Mode::SelectedOpening, {}},
        Case{"selected-card", Mode::SelectedCard, {}},
        Case{"saved-marker", Mode::Saved, {}}};
    struct State final
    {
        Application& owner;
        Mode mode;
        ContinuationReceipt receipt;
        std::optional<ContinuationReceipt> younger;
        std::shared_ptr<SelectionMoveCloseTestGate> close;
        std::shared_ptr<SizingWorkTestGate> size;
        std::optional<CanvasDocument> before, mutated;
        DocumentHistory::StateView history, mutatedHistory;
        std::optional<std::uint64_t> opening, ordinal;
        std::uint64_t generation = 0, mutatedGeneration = 0;
        bool closeVisited = false, closeHeld = false, sizeVisited = false, sizeHeld = false;
        bool eventExact = false, cancelPending = false, youngerPending = false, pumpPending = false;
        bool zoomChanged = false, mutationReached = false;
    };
    constexpr UINT kSizeOutlinePumpMessage = WM_APP + 0x4cd;
    const auto originalViewport = _viewport;
    const auto originalForeground = _session.ForegroundColor(), originalBackground = _session.BackgroundColor();
    const auto restore = wil::scope_exit(
        [this, originalViewport, originalForeground, originalBackground]() noexcept
        {
            if (_selectionMoveCloseTestGate && _selectionMoveCloseTestGate->release)
                (void)SetEvent(_selectionMoveCloseTestGate->release.get());
            if (_sizingWorkTestGate && _sizingWorkTestGate->release)
                (void)SetEvent(_sizingWorkTestGate->release.get());
            if (!_jobs.IsIdle())
            {
                CancelHostContinuations();
                if (SettleJobs(_jobs, 15000, DispatchMessageThunk, this) != SettleResult::Settled)
                    SelfTestFailure(L"Size outline fault cleanup did not acknowledge its workers.");
            }
            _viewport = originalViewport;
            _session.SetForegroundColor(originalForeground);
            _session.SetBackgroundColor(originalBackground);
            _selectionMoveCloseTestGate.reset();
            _sizingWorkTestGate.reset();
            _hostAdmissionAllocationFailureForTest = false;
            if (_session.IsMovingSelection())
                _session.CancelSelectionMove();
            _session.CancelColorEdit();
            CancelSizeCard();
        });
    int assertions = 0, failures = 0, modes = 0;
    const auto expect = [&assertions, &failures](const char* mode, const char* check, bool value) noexcept
    {
        ++assertions;
        failures += value ? 0 : 1;
        std::fprintf(stderr, "%s selection-size-outline-fault.%s.%s\n", value ? "PASS" : "FAIL", mode, check);
        return value;
    };
    const auto prepare = [this](bool image) -> bool
    {
        _session.ClearDocument();
        _session.CreateDocument(40, 40, false);
        (void)InsertPaintedLayer(_session, L"Size outline pixels", 40, 40, Vec2{0, 0}, 0, 0xFF808080);
        _session.SetSelection(SizeOutlineSquare(10, 10), L"Size outline fixture");
        _session.history.Reset();
        _session.history.MarkSaved();
        _session.SetForegroundColor(0xFFFF0000);
        _session.SetBackgroundColor(0xFF00FF00);
        DocumentChanged();
        _viewport.zoom = 1;
        _viewport.followsFit = false;
        OpenSizeCard(image ? SizeCardKind::ImageSize : SizeCardKind::CanvasSize);
        OnSizeAction(SizeAction::SetLocked, 0);
        OnSizeAction(SizeAction::SetWidth, image ? 80 : 50);
        OnSizeAction(SizeAction::SetHeight, image ? 80 : 40);
        if (image)
        {
            OnSizeAction(SizeAction::SetSampling, 0);
            OnSizeAction(SizeAction::SetResolution, 144);
        }
        else
            OnSizeAction(SizeAction::SetAnchor, 0);
        if (!_session.BeginSelectionMove())
            return false;
        _session.MoveSelection(5, 7);
        return SizeCardOpen() && _session.Selection() == std::optional(SizeOutlineSquare(15, 17));
    };
    const auto pump = [](void* opaque, void* native) noexcept
    {
        auto& state = *static_cast<State*>(opaque);
        auto& owner = state.owner;
        auto& message = *static_cast<MSG*>(native);
        if (message.message == SelectionMoveCloseTestGate::kMessage)
        {
            state.closeVisited = true;
            const auto* input = owner._activeHostRequest
                                    ? std::get_if<HostSizeApplyInput>(&owner._activeHostRequest->input.Input())
                                    : nullptr;
            state.ordinal = owner._activeHostRequest ? std::optional(owner._activeHostRequest->ordinal) : std::nullopt;
            const auto color = state.mode == Mode::Foreground   ? std::optional<std::uint32_t>(0xFFFF0000)
                               : state.mode == Mode::Background ? std::optional<std::uint32_t>(0xFF00FF00)
                                                                : std::nullopt;
            state.eventExact = input && input->selectionMoveOpening == state.opening &&
                               input->invocation == owner._sizeInvocation && input->extensionArgb == color;
            state.closeHeld =
                owner._selectionMoveCloseTicket && owner._selectionMoveCloseStamp && owner._historyWork.Busy() &&
                !owner._sizingTicket && !owner._sizingStamp && state.receipt.Read().outcome == HostOutcome::Pending &&
                owner._session.Document() == state.before && owner._session.IsMovingSelection() &&
                owner._session.Generation() == state.generation && owner._session.history.IsCurrent(state.history) &&
                state.close->captures.load() == 1 && state.close->reservationHeld.load() &&
                owner._jobs.ReservedMemoryBytes() >= state.close->reservationBytes;
            if (state.mode == Mode::Foreground || state.mode == Mode::Background)
            {
                owner._session.SetForegroundColor(0xFF0000FF);
                owner._session.SetBackgroundColor(0xFF0000FF);
            }
            if (state.mode == Mode::Ordered)
            {
                state.younger = owner.QueueHostInput(HostCommandInput{ChromeHost::kCommandUndo});
                state.youngerPending = state.younger->Read().outcome == HostOutcome::Pending;
            }
            if (state.mode == Mode::Pump)
            {
                const auto zoom = owner._viewport.zoom;
                owner.OnCommand(ChromeHost::kCommandZoomIn);
                state.zoomChanged = owner._viewport.zoom != zoom && owner._historyWork.Busy();
                if (PostMessageW(owner._window.get(), kSizeOutlinePumpMessage, 0, 0))
                    return;
            }
            if (state.mode == Mode::CloseCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending &&
                                      owner._selectionMoveCloseTicket && owner._historyWork.Busy();
            }
            if (state.mode == Mode::Opening)
            {
                owner._session.CancelSelectionMove();
                state.mutationReached = owner._session.BeginSelectionMove();
                owner._session.MoveSelection(-2, 1);
            }
            if (state.mode == Mode::Card)
            {
                owner.CancelSizeCard();
                owner.OpenSizeCard(SizeCardKind::ImageSize);
                state.mutationReached = owner.SizeCardOpen();
            }
            if (state.mode == Mode::Generation)
            {
                owner._session.SetOpacity(0.5);
                state.mutationReached = owner._session.ActiveLayer()->opacity == 0.5;
            }
            if (state.mode == Mode::HeldCoexisting)
            {
                owner._session.BeginColorEdit(FilterKind::GaussianBlur, true);
                state.mutationReached = owner._session.HasColorEdit();
            }
            if (state.mode == Mode::Saved)
                owner._session.history.MarkSaved();
            state.mutated = owner._session.Document();
            state.mutatedHistory = owner._session.history.CaptureState();
            state.mutatedGeneration = owner._session.Generation();
            (void)SetEvent(state.close->release.get());
            return;
        }
        if (message.message == SizingWorkTestGate::kMessage)
        {
            state.sizeVisited = true;
            state.sizeHeld = owner._activeHostRequest && state.ordinal &&
                             owner._activeHostRequest->ordinal == state.ordinal.value() &&
                             state.receipt.Read().outcome == HostOutcome::Pending && owner._sizingTicket &&
                             owner._sizingStamp && !owner._selectionMoveCloseTicket && owner._historyWork.Busy() &&
                             owner._session.Document()->width == 40 && owner._session.Document()->height == 40 &&
                             owner._session.Selection() == std::optional(SizeOutlineSquare(10, 10)) &&
                             !owner._session.IsMovingSelection() && owner._session.history.UndoCount() == 0 &&
                             state.size->captureReservationHeld.load() &&
                             owner._jobs.ReservedMemoryBytes() >= state.size->reservationBytes;
            if (state.mode == Mode::SizeCancel)
            {
                owner.CancelHostContinuations();
                state.cancelPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._sizingTicket &&
                                      owner._historyWork.Busy();
            }
            if (state.mode == Mode::Ordered)
                state.youngerPending =
                    state.youngerPending && state.younger && state.younger->Read().outcome == HostOutcome::Pending;
            (void)SetEvent(state.size->release.get());
            return;
        }
        if (message.message == kSizeOutlinePumpMessage)
        {
            state.pumpPending = state.receipt.Read().outcome == HostOutcome::Pending && owner._historyWork.Busy() &&
                                owner._session.Document() == state.before;
            (void)SetEvent(state.close->release.get());
            return;
        }
        owner.DispatchQueuedMessage(message);
    };
    for (const Case& test : cases)
    {
        ++modes;
        if (!expect(test.name, "fixture", prepare(test.mode == Mode::Image)))
            return 5;
        if (test.mode == Mode::Foreground || test.mode == Mode::Background)
            OnSizeAction(SizeAction::SetExtension, test.mode == Mode::Foreground ? 1 : 2);
        State state{*this, test.mode};
        state.before = _session.Document();
        state.history = _session.history.CaptureState();
        state.opening = _session.SelectionMoveOpening();
        state.generation = _session.Generation();
        state.close = std::make_shared<SelectionMoveCloseTestGate>();
        state.close->release.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        state.size = std::make_shared<SizingWorkTestGate>();
        if (!state.close->release || !state.size->release)
            return 5;
        if (test.mode == Mode::CloseFailure)
            state.close->result = {JobStatus::Failed, E_ACCESSDENIED};
        state.close->throwCaptureFailure = test.mode == Mode::CloseCapture;
        if (test.mode == Mode::SizeFailure)
            state.size->result = {JobStatus::Failed, E_ACCESSDENIED};
        state.size->throwCaptureFailure = test.mode == Mode::SizeCapture;
        state.size->historyFailure = test.stage;
        _selectionMoveCloseTestGate = state.close;
        _sizingWorkTestGate = state.size;
        state.receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
        const bool selectedStale = test.mode == Mode::SelectedCoexisting || test.mode == Mode::SelectedClosed ||
                                   test.mode == Mode::SelectedOpening || test.mode == Mode::SelectedCard;
        if (test.mode == Mode::SelectedCoexisting)
        {
            _session.BeginColorEdit(FilterKind::GaussianBlur, true);
            state.mutationReached = _session.HasColorEdit();
        }
        if (test.mode == Mode::SelectedClosed || test.mode == Mode::SelectedOpening)
        {
            _session.CancelSelectionMove();
            state.mutationReached = !_session.IsMovingSelection();
            if (test.mode == Mode::SelectedOpening)
            {
                state.mutationReached = _session.BeginSelectionMove();
                _session.MoveSelection(-2, 1);
            }
        }
        if (test.mode == Mode::SelectedCard)
        {
            CancelSizeCard();
            OpenSizeCard(SizeCardKind::ImageSize);
            state.mutationReached = SizeCardOpen();
        }
        if (selectedStale)
        {
            state.mutated = _session.Document();
            state.mutatedHistory = _session.history.CaptureState();
            state.mutatedGeneration = _session.Generation();
        }
        if (!expect(test.name, "settled", SettleJobs(_jobs, 15000, pump, &state) == SettleResult::Settled))
            return 5;
        const bool stale = selectedStale || test.mode == Mode::Opening || test.mode == Mode::Card ||
                           test.mode == Mode::Generation || test.mode == Mode::HeldCoexisting;
        const bool closeFailed = test.mode == Mode::CloseFailure || test.mode == Mode::CloseCapture;
        const bool sizeFailed =
            test.mode == Mode::SizeFailure || test.mode == Mode::SizeCapture || test.mode == Mode::SizeHistory;
        const auto expected = stale                                                             ? HostOutcome::Stale
                              : test.mode == Mode::CloseCancel || test.mode == Mode::SizeCancel ? HostOutcome::Cancelled
                              : closeFailed || sizeFailed ? HostOutcome::Failed
                                                          : HostOutcome::Succeeded;
        expect(test.name, "terminal-outcome", state.receipt.Read().outcome == expected);
        expect(test.name, "all-owners-and-budget-released",
               !_historyWork.Busy() && !_selectionMoveCloseTicket && !_selectionMoveCloseStamp && !_sizingTicket &&
                   !_sizingStamp && !_activeHostRequest && _jobs.ReservedMemoryBytes() == 0 &&
                   _hostInputBudget.Used() == 0);
        if (selectedStale)
            expect(test.name, "selected-refusal-before-capture",
                   state.mutationReached && !state.closeVisited && state.close->captures.load() == 0 &&
                       !state.close->retired.load() && !state.sizeVisited && _session.Document() == state.mutated &&
                       _session.Generation() == state.mutatedGeneration &&
                       _session.history.IsCurrent(state.mutatedHistory));
        else
        {
            expect(test.name, "capture-reservation-and-worker-disposal",
                   state.close->reservationHeld.load() && state.close->retired.load() &&
                       state.close->retiredOffOwner.load());
            if (test.mode != Mode::CloseCapture)
                expect(test.name, "held-close-retains-live-state-before-size",
                       state.closeVisited && state.closeHeld && state.eventExact && state.close->captures.load() == 1);
            else
                expect(test.name, "capture-failure-never-runs-close",
                       !state.closeVisited && state.close->captures.load() == 0);
        }
        if (stale)
        {
            expect(test.name, "stale-does-not-start-resize",
                   !state.sizeVisited && !state.size->payloadRetired.load() && state.mutationReached &&
                       _session.Document() == state.mutated && _session.Generation() == state.mutatedGeneration &&
                       _session.history.IsCurrent(state.mutatedHistory));
            if (test.mode == Mode::HeldCoexisting || test.mode == Mode::SelectedCoexisting)
                expect(test.name, "new-color-opening-survives", _session.HasColorEdit());
        }
        else if (closeFailed || test.mode == Mode::CloseCancel)
            expect(test.name, "close-refusal-keeps-original-live-opening",
                   _session.Document() == state.before && _session.SelectionMoveOpening() == state.opening &&
                       _session.Generation() == state.generation && _session.history.IsCurrent(state.history) &&
                       !state.sizeVisited && !state.size->payloadRetired.load());
        else
        {
            expect(test.name, "second-worker-uses-original-outline-and-same-ordinal",
                   test.mode == Mode::SizeCapture ? !state.sizeVisited : state.sizeVisited && state.sizeHeld);
            expect(test.name, "sizing-reservation-and-worker-disposal",
                   state.size->captureReservationHeld.load() && state.size->payloadRetired.load() &&
                       state.size->payloadRetiredOffOwner.load());
            if (sizeFailed || test.mode == Mode::SizeCancel)
                expect(test.name, "size-refusal-keeps-cancelled-origin-without-entry",
                       _session.Document()->width == 40 && _session.Document()->height == 40 &&
                           _session.Selection() == std::optional(SizeOutlineSquare(10, 10)) &&
                           !_session.IsMovingSelection() && _session.history.UndoCount() == 0);
            else if (test.mode != Mode::Ordered)
                expect(test.name, "literal-success",
                       _session.Document()->width == (test.mode == Mode::Image ? 80 : 50) &&
                           _session.Document()->height == (test.mode == Mode::Image ? 80 : 40) &&
                           !_session.Selection() && !_session.IsMovingSelection() && _session.history.UndoCount() == 1);
        }
        if (test.mode == Mode::CloseFailure || test.mode == Mode::SizeFailure)
            expect(test.name, "native-worker-error", state.receipt.Read().error == E_ACCESSDENIED);
        if (test.mode == Mode::CloseCapture || test.mode == Mode::SizeCapture || test.mode == Mode::SizeHistory)
            expect(test.name, "allocation-error", state.receipt.Read().error == E_OUTOFMEMORY);
        if (test.mode == Mode::SizeHistory)
            expect(test.name, "real-sizing-history-fault", state.size->historyFaultHits.load() == 1);
        if (test.mode == Mode::CloseCancel || test.mode == Mode::SizeCancel)
            expect(test.name, "cancel-waits-for-worker-ack", state.cancelPending);
        if (test.mode == Mode::Pump)
            expect(test.name, "view-and-posted-message-remain-live", state.zoomChanged && state.pumpPending);
        if (test.mode == Mode::Ordered)
        {
            expect(test.name, "younger-undo-waits-for-both-workers",
                   state.youngerPending && state.younger && state.younger->Read().outcome == HostOutcome::Succeeded &&
                       _session.Document()->width == 40 &&
                       _session.Selection() == std::optional(SizeOutlineSquare(10, 10)) &&
                       _session.history.UndoCount() == 0 && _session.history.RedoName() == L"Canvas Size");
            _session.Redo();
            expect(test.name, "ordered-redo-literal-resized-document",
                   _session.Document()->width == 50 && !_session.Selection() &&
                       _session.history.UndoName() == L"Canvas Size");
        }
        if (test.mode == Mode::Foreground || test.mode == Mode::Background)
        {
            std::array<std::uint8_t, 4> pixel{};
            const auto& document = _session.Document().value();
            if (!document.layers.empty() && document.layers.front().asset)
                document.layers.front().asset->image.ReadRegion(45, 0, 1, 1, pixel.data(), 4, 0);
            expect(test.name, "literal-input-time-extension-pixel",
                   pixel == (test.mode == Mode::Foreground ? std::array<std::uint8_t, 4>{0, 0, 255, 255}
                                                           : std::array<std::uint8_t, 4>{0, 255, 0, 255}));
        }
        if (test.mode == Mode::Saved)
        {
            expect(test.name, "resize-is-modified-after-save-marker", _session.history.IsModified());
            _session.Undo();
            expect(test.name, "latest-saved-marker-and-origin-survive",
                   !_session.history.IsModified() && _session.Selection() == std::optional(SizeOutlineSquare(10, 10)));
        }
        _selectionMoveCloseTestGate.reset();
        _sizingWorkTestGate.reset();
        _session.CancelColorEdit();
        if (_session.IsMovingSelection())
            _session.CancelSelectionMove();
        CancelSizeCard();
    }
    for (const bool allocation : {false, true})
    {
        ++modes;
        const char* name = allocation ? "input-allocation" : "input-budget";
        if (!prepare(false))
            return 5;
        const auto document = _session.Document();
        const auto history = _session.history.CaptureState();
        const auto generation = _session.Generation();
        std::optional<HostInputBudget::Reservation> reservation;
        if (allocation)
            _hostAdmissionAllocationFailureForTest = true;
        else
            reservation = _hostInputBudget.TryReserve(HostInputBudget::kStorageLimit);
        ApplySizeCard();
        _hostAdmissionAllocationFailureForTest = false;
        expect(name, "actual-apply-refuses-without-owner-work",
               (allocation || reservation) && _session.Document() == document && _session.history.IsCurrent(history) &&
                   _session.Generation() == generation && !_activeHostRequest && _hostRequests.Size() == 0 &&
                   !_historyWork.Busy() && SizeCardOpen());
        reservation.reset();
        ApplySizeCard();
        if (!expect(name, "retry-settles",
                    SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
            return 5;
        expect(name, "fresh-apply-retry-resizes",
               _session.Document()->width == 50 && !_session.Selection() && _session.history.UndoCount() == 1 &&
                   _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0);
    }
    ++modes;
    if (!prepare(false))
        return 5;
    const auto document = _session.Document();
    const auto history = _session.history.CaptureState();
    const auto generation = _session.Generation();
    auto gate = std::make_shared<SelectionMoveCloseTestGate>();
    _selectionMoveCloseTestGate = gate;
    for (std::size_t record = 0; record < JobScheduler::kCapacity; ++record)
    {
        std::unique_ptr<Job> job = std::make_unique<OccupySizeOutlineSlot>();
        JobTicket ticket;
        if (FAILED(_jobs.Submit(job, {JobClass::Background}, ticket)))
            return 5;
    }
    const auto receipt = QueueHostInput(HostSizeApplyInput{_sizeDialog, _sizeInvocation});
    for (unsigned int pass = 0; pass < 4 && receipt.Read().outcome == HostOutcome::Pending; ++pass)
        (void)RunUiActions(_jobs);
    expect("scheduler-admission", "full-record-table-refuses-before-capture",
           receipt.Read().outcome == HostOutcome::Failed &&
               receipt.Read().error == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA)) &&
               _session.Document() == document && _session.Generation() == generation &&
               _session.history.IsCurrent(history) && gate->captures.load() == 0 && !gate->retired.load() &&
               !_historyWork.Busy() && SizeCardOpen());
    if (!expect("scheduler-admission", "occupied-records-settle",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
        return 5;
    _selectionMoveCloseTestGate.reset();
    ApplySizeCard();
    if (!expect("scheduler-admission", "fresh-retry-settles",
                SettleJobs(_jobs, 15000, DispatchMessageThunk, this) == SettleResult::Settled))
        return 5;
    expect("scheduler-admission", "fresh-retry-resizes",
           _session.Document()->width == 50 && !_session.Selection() && _session.history.UndoCount() == 1 &&
               _jobs.ReservedMemoryBytes() == 0 && _hostInputBudget.Used() == 0);
    std::fprintf(stderr, "SUMMARY selection-size-outline-fault assertions=%d failures=%d modes=%d\n", assertions,
                 failures, modes);
    return failures ? 5 : 0;
}
